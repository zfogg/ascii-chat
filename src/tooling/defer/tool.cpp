#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llvm;
using namespace clang;

namespace fs = std::filesystem;

std::mutex &outputRegistryMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_set<std::string> &outputRegistry() {
  static std::unordered_set<std::string> registry;
  return registry;
}

bool registerOutputPath(const std::string &path) {
  std::lock_guard<std::mutex> guard(outputRegistryMutex());
  auto &registry = outputRegistry();
  auto [_, inserted] = registry.insert(path);
  return inserted;
}

void unregisterOutputPath(const std::string &path) {
  std::lock_guard<std::mutex> guard(outputRegistryMutex());
  outputRegistry().erase(path);
}

// Command line options
static cl::OptionCategory ToolCategory("ascii-defer transformation options");
static cl::extrahelp CommonHelp(tooling::CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nDefer transformation tool for ascii-chat\n");

static cl::opt<std::string> OutputDirectoryOption("output-dir",
                                                  cl::desc("Directory where transformed sources will be written"),
                                                  cl::value_desc("path"), cl::Required, cl::cat(ToolCategory));

static cl::opt<std::string>
    InputRootOption("input-root", cl::desc("Root directory of original sources (used to compute relative paths)"),
                    cl::value_desc("path"), cl::init(""), cl::cat(ToolCategory));

static cl::opt<std::string> BuildPath("p", cl::desc("Build path (directory containing compile_commands.json)"),
                                      cl::Optional, cl::cat(ToolCategory));

static cl::list<std::string> SourcePaths(cl::Positional, cl::desc("<source0> [... <sourceN>]"), cl::cat(ToolCategory));

namespace {

// Structure to track defer calls within a function
struct DeferCall {
  SourceLocation location;
  std::string expression;
  std::string functionName;
  unsigned scopeId;
};

// Structure to track function transformation state
struct FunctionTransformState {
  FunctionDecl *funcDecl = nullptr;
  std::vector<DeferCall> deferCalls;
  std::set<SourceLocation> returnLocations;
  bool needsTransformation = false;
  unsigned nextScopeId = 0;
};

class DeferVisitor : public RecursiveASTVisitor<DeferVisitor> {
public:
  DeferVisitor(ASTContext &context, Rewriter &rewriter, const fs::path &outputDir, const fs::path &inputRoot)
      : context_(context), rewriter_(rewriter), outputDir_(outputDir), inputRoot_(inputRoot) {}

  bool TraverseFunctionDecl(FunctionDecl *funcDecl) {
    if (!funcDecl || funcDecl->isImplicit()) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseFunctionDecl(funcDecl);
    }

    SourceManager &sourceManager = context_.getSourceManager();
    SourceLocation location = funcDecl->getLocation();
    location = sourceManager.getExpansionLoc(location);
    if (!location.isValid() || !sourceManager.isWrittenInMainFile(location)) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseFunctionDecl(funcDecl);
    }

    // Start tracking this function
    currentFunction_ = FunctionTransformState();
    currentFunction_.funcDecl = funcDecl;

    bool result = RecursiveASTVisitor<DeferVisitor>::TraverseFunctionDecl(funcDecl);

    // Transform function if it contains defer calls
    if (currentFunction_.needsTransformation && !currentFunction_.deferCalls.empty()) {
      transformFunction(currentFunction_);
    }

    currentFunction_ = FunctionTransformState();
    return result;
  }

  bool TraverseStmt(Stmt *stmt) {
    if (!stmt || !currentFunction_.funcDecl) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseStmt(stmt);
    }

    SourceManager &sourceManager = context_.getSourceManager();
    SourceLocation stmtLoc = stmt->getBeginLoc();

    if (stmtLoc.isValid() && sourceManager.isWrittenInMainFile(stmtLoc)) {
      // Get the source text for this statement
      SourceLocation begin = stmt->getBeginLoc();
      SourceLocation end = stmt->getEndLoc();

      if (begin.isValid() && end.isValid()) {
        // Get the source range
        CharSourceRange range = CharSourceRange::getTokenRange(begin, end);
        bool invalid = false;
        StringRef stmtText = Lexer::getSourceText(range, sourceManager, context_.getLangOpts(), &invalid);

        if (!invalid && stmtText.contains("defer(")) {
          // Found a defer() call - extract it
          size_t deferPos = stmtText.find("defer(");
          if (deferPos != StringRef::npos) {
            // Find matching closing parenthesis
            size_t openParen = deferPos + 5; // after "defer"
            size_t closeParen = findMatchingParen(stmtText, openParen);

            if (closeParen != StringRef::npos) {
              // Extract the expression inside defer(...)
              StringRef expression = stmtText.substr(openParen + 1, closeParen - openParen - 1);

              // Calculate the actual source location of "defer" in the file
              SourceLocation deferLoc = begin.getLocWithOffset(deferPos);

              DeferCall deferCall;
              deferCall.location = deferLoc;
              deferCall.expression = expression.str();
              deferCall.functionName = extractFunctionNameFromText(expression.str());
              deferCall.scopeId = currentFunction_.nextScopeId;

              currentFunction_.deferCalls.push_back(deferCall);
              currentFunction_.needsTransformation = true;

              llvm::errs() << "Found defer() call: " << expression << " at offset " << deferPos << "\n";
            }
          }
        }
      }
    }

    return RecursiveASTVisitor<DeferVisitor>::TraverseStmt(stmt);
  }

  bool TraverseReturnStmt(ReturnStmt *returnStmt) {
    if (returnStmt && currentFunction_.funcDecl) {
      SourceManager &sourceManager = context_.getSourceManager();
      SourceLocation location = returnStmt->getReturnLoc();
      if (location.isValid()) {
        SourceLocation expansionLocation = sourceManager.getExpansionLoc(location);
        if (expansionLocation.isValid() && sourceManager.isWrittenInMainFile(expansionLocation)) {
          currentFunction_.returnLocations.insert(expansionLocation);
        }
      }
    }
    return RecursiveASTVisitor<DeferVisitor>::TraverseReturnStmt(returnStmt);
  }

  bool includeNeeded() const {
    return includeNeeded_;
  }

  std::string makeRelativePath(const fs::path &absolutePath) const {
    if (inputRoot_.empty()) {
      return absolutePath.generic_string();
    }

    std::error_code errorCode;
    fs::path relative = fs::relative(absolutePath, inputRoot_, errorCode);
    if (errorCode) {
      return absolutePath.generic_string();
    }
    return relative.generic_string();
  }

private:
  std::string extractExpression(Expr *expr) {
    if (!expr) {
      return "";
    }

    SourceManager &sourceManager = context_.getSourceManager();
    SourceLocation begin = expr->getBeginLoc();
    SourceLocation end = expr->getEndLoc();

    if (begin.isInvalid() || end.isInvalid()) {
      return "";
    }

    CharSourceRange range = CharSourceRange::getTokenRange(begin, end);
    bool invalid = false;
    StringRef text = Lexer::getSourceText(range, sourceManager, context_.getLangOpts(), &invalid);

    if (invalid || text.empty()) {
      return "";
    }

    return text.str();
  }

  std::string extractFunctionName(Expr *expr) {
    if (!expr) {
      return "";
    }

    // Try to find a function call in the expression
    if (CallExpr *callExpr = dyn_cast<CallExpr>(expr->IgnoreParenImpCasts())) {
      if (Expr *callee = callExpr->getCallee()) {
        if (DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(callee->IgnoreParenImpCasts())) {
          if (declRef->getDecl()) {
            return declRef->getDecl()->getNameAsString();
          }
        }
      }
    }

    return "";
  }

  std::string extractFunctionNameFromText(const std::string &expression) const {
    // Simple text extraction: find function_name(...)
    size_t parenPos = expression.find('(');
    if (parenPos == std::string::npos) {
      return "";
    }

    // Extract everything before the '(' and trim whitespace
    std::string funcName = expression.substr(0, parenPos);
    funcName.erase(0, funcName.find_first_not_of(" \t\n\r"));
    funcName.erase(funcName.find_last_not_of(" \t\n\r") + 1);

    return funcName;
  }

  size_t findMatchingParen(StringRef text, size_t openPos) const {
    if (openPos >= text.size() || text[openPos] != '(') {
      return StringRef::npos;
    }

    int depth = 1;
    for (size_t i = openPos + 1; i < text.size(); i++) {
      if (text[i] == '(') {
        depth++;
      } else if (text[i] == ')') {
        depth--;
        if (depth == 0) {
          return i;
        }
      }
    }

    return StringRef::npos;
  }

  std::string generateScopeInit() const {
    return "\n    ascii_defer_scope_t __defer_scope_0;\n"
           "    ascii_defer_scope_init(&__defer_scope_0);\n";
  }

  std::string generateCleanupBeforeReturn() const {
    return "ascii_defer_execute_all(&__defer_scope_0); ";
  }

  std::string generateCleanupAtEnd() const {
    return "    ascii_defer_execute_all(&__defer_scope_0);\n";
  }

  void transformFunction(FunctionTransformState &state) {
    if (!state.funcDecl || state.deferCalls.empty()) {
      return;
    }

    Stmt *body = state.funcDecl->getBody();
    if (!body) {
      return;
    }

    CompoundStmt *compoundBody = dyn_cast<CompoundStmt>(body);
    if (!compoundBody) {
      return;
    }

    SourceManager &sourceManager = context_.getSourceManager();

    // Step 1: Insert defer scope declaration and initialization at function entry
    SourceLocation bodyStart = compoundBody->getLBracLoc().getLocWithOffset(1);
    if (bodyStart.isValid()) {
      rewriter_.InsertText(bodyStart, generateScopeInit(), true, true);
      includeNeeded_ = true;
    }

    // Step 2: Transform each defer() call to runtime API
    for (const DeferCall &deferCall : state.deferCalls) {
      transformDeferCall(deferCall);
    }

    // Step 3: Insert cleanup before each return statement
    for (SourceLocation returnLoc : state.returnLocations) {
      rewriter_.InsertText(returnLoc, generateCleanupBeforeReturn(), true, true);
    }

    // Step 4: Insert cleanup at function end (if no explicit return)
    SourceLocation bodyEnd = compoundBody->getRBracLoc();
    if (bodyEnd.isValid()) {
      rewriter_.InsertText(bodyEnd, generateCleanupAtEnd(), true, true);
    }
  }

  std::string generateDeferReplacement(unsigned ctxId, const std::string &expression,
                                       const std::string &functionName, const std::string &args) const {
    std::string code = "{\n";
    code += "        // Defer: " + expression + "\n";
    code += "        void *__defer_fn_" + std::to_string(ctxId);
    code += " = (void*)(" + functionName + ");\n";
    code += "        void *__defer_ctx_" + std::to_string(ctxId);
    code += " = (void*)(" + args + ");\n";
    code += "        ascii_defer_push(&__defer_scope_0, (ascii_defer_fn_t)__defer_fn_";
    code += std::to_string(ctxId) + ", &__defer_ctx_";
    code += std::to_string(ctxId) + ", sizeof(__defer_ctx_";
    code += std::to_string(ctxId) + "));\n";
    code += "    }";
    return code;
  }

  std::string generateDeferReplacementNoFunction(unsigned ctxId, const std::string &expression) const {
    std::string code = "{\n";
    code += "        // Defer: " + expression + "\n";
    code += "        // WARNING: Non-function defer may not work correctly\n";
    code += "        void *__defer_expr_" + std::to_string(ctxId);
    code += " = (void*)(" + expression + ");\n";
    code += "        ascii_defer_push(&__defer_scope_0, (ascii_defer_fn_t)NULL, ";
    code += "&__defer_expr_" + std::to_string(ctxId) + ", sizeof(__defer_expr_";
    code += std::to_string(ctxId) + "));\n";
    code += "    }";
    return code;
  }

  void transformDeferCall(const DeferCall &deferCall) {
    SourceManager &sourceManager = context_.getSourceManager();

    // Find the extent of the defer() macro invocation
    SourceLocation macroLoc = deferCall.location;
    if (!macroLoc.isValid()) {
      return;
    }

    // Get the range covering "defer(expression);"
    // We need to find the semicolon after the defer call
    FileID fileId = sourceManager.getFileID(macroLoc);
    bool invalid = false;
    StringRef fileData = sourceManager.getBufferData(fileId, &invalid);
    if (invalid) {
      return;
    }

    unsigned offset = sourceManager.getFileOffset(macroLoc);
    size_t semicolonPos = fileData.find(';', offset);
    if (semicolonPos == StringRef::npos) {
      return;
    }

    SourceLocation semicolonLoc = macroLoc.getLocWithOffset(semicolonPos - offset);
    CharSourceRange deferRange = CharSourceRange::getCharRange(macroLoc, semicolonLoc.getLocWithOffset(1));

    // Generate replacement code
    unsigned ctxId = deferCallCounter_++;
    std::string replacement;

    if (!deferCall.functionName.empty()) {
      // Extract the function arguments
      size_t parenPos = deferCall.expression.find('(');
      if (parenPos != std::string::npos) {
        size_t closeParenPos = deferCall.expression.rfind(')');
        if (closeParenPos != std::string::npos && closeParenPos > parenPos) {
          std::string args = deferCall.expression.substr(parenPos + 1, closeParenPos - parenPos - 1);

          // Trim whitespace
          args.erase(0, args.find_first_not_of(" \t\n\r"));
          args.erase(args.find_last_not_of(" \t\n\r") + 1);

          replacement = generateDeferReplacement(ctxId, deferCall.expression,
                                                 deferCall.functionName, args);
        }
      }
    } else {
      replacement = generateDeferReplacementNoFunction(ctxId, deferCall.expression);
    }

    if (!replacement.empty()) {
      rewriter_.ReplaceText(deferRange, replacement);
    }
  }

  ASTContext &context_;
  Rewriter &rewriter_;
  fs::path outputDir_;
  fs::path inputRoot_;
  FunctionTransformState currentFunction_;
  bool includeNeeded_ = false;
  unsigned deferCallCounter_ = 0;
};

class DeferASTConsumer : public ASTConsumer {
public:
  explicit DeferASTConsumer(DeferVisitor &visitor) : visitor_(visitor) {}

  void HandleTranslationUnit(ASTContext &context) override {
    visitor_.TraverseDecl(context.getTranslationUnitDecl());
  }

private:
  DeferVisitor &visitor_;
};

class DeferFrontendAction : public ASTFrontendAction {
public:
  explicit DeferFrontendAction(const fs::path &outputDir, const fs::path &inputRoot)
      : outputDir_(outputDir), inputRoot_(inputRoot) {
    initializeProtectedDirectories();
  }

  void EndSourceFileAction() override {
    SourceManager &sourceManager = rewriter_.getSourceMgr();
    const FileEntry *fileEntry = sourceManager.getFileEntryForID(sourceManager.getMainFileID());
    if (!fileEntry) {
      return;
    }

    if (!visitor_) {
      llvm::errs() << "Defer visitor not initialized; skipping file output\n";
      hadWriteError_ = true;
      return;
    }

    const StringRef filePathRef = fileEntry->tryGetRealPathName();
    if (filePathRef.empty()) {
      llvm::errs() << "Unable to resolve file path for transformed output\n";
      return;
    }

    const fs::path originalPath = fs::path(filePathRef.str());
    const std::string relativePath = visitor_->makeRelativePath(originalPath);




    fs::path destinationPath = outputDir_ / relativePath;

    // SAFETY CHECK: Never overwrite source files
    std::error_code ec;
    fs::path canonicalOriginal = fs::canonical(originalPath, ec);
    if (!ec) {
      fs::path canonicalDest = fs::weakly_canonical(destinationPath, ec);
      if (!ec && canonicalOriginal == canonicalDest) {
        llvm::errs() << "ERROR: Output path is the same as source file! Refusing to overwrite source.\n";
        llvm::errs() << "  Source: " << canonicalOriginal.string() << "\n";
        llvm::errs() << "  Output: " << canonicalDest.string() << "\n";
        hadWriteError_ = true;
        return;
      }
    }

    // Make absolute if not already (relative to current working directory at tool invocation)
    if (!destinationPath.is_absolute()) {
      llvm::SmallString<256> absPath;
      llvm::sys::fs::real_path(destinationPath.string(), absPath);
      // If real_path fails (file doesn't exist), make it absolute relative to CWD
      if (absPath.empty()) {
        llvm::SmallString<256> cwd;
        llvm::sys::fs::current_path(cwd);
        llvm::sys::path::append(cwd, destinationPath.string());
        destinationPath = fs::path(std::string(cwd));
      } else {
        destinationPath = fs::path(std::string(absPath));
      }
    }

    // Use generic_string() for forward slashes on all platforms
    const std::string destinationString = destinationPath.generic_string();



    if (!registerOutputPath(destinationString)) {
      return;
    }


    // Check file existence using LLVM's exists() inline function
    bool fileExists = llvm::sys::fs::exists(llvm::Twine(destinationString));


    if (fileExists && isInProtectedSourceTree(destinationPath)) {
      llvm::errs() << "Refusing to overwrite existing file in protected source tree: " << destinationString << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
      return;
    }

    const fs::path parent = destinationPath.parent_path();
    std::error_code directoryError;
    fs::create_directories(parent, directoryError);
    if (directoryError) {
      llvm::errs() << "Failed to create output directory: " << parent.string() << " - " << directoryError.message()
                   << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
      return;
    }

    ensureIncludeInserted(originalPath);

    std::string rewrittenContents;
    if (const RewriteBuffer *buffer = rewriter_.getRewriteBufferFor(sourceManager.getMainFileID())) {
      rewrittenContents.assign(buffer->begin(), buffer->end());

    } else {
      rewrittenContents = sourceManager.getBufferData(sourceManager.getMainFileID()).str();

    }


    std::error_code fileError;
    llvm::raw_fd_ostream outputStream(destinationPath.string(), fileError, llvm::sys::fs::OF_Text);
    if (fileError) {
      llvm::errs() << "Failed to open output file: " << destinationString << " - " << fileError.message() << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
      return;
    }

    outputStream << rewrittenContents;
    outputStream.close();
    if (outputStream.has_error()) {
      llvm::errs() << "Error while writing transformed file: " << destinationString << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
    } else {
    }
  }

  bool hadWriteError() const {
    return hadWriteError_;
  }

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler, StringRef) override {
    rewriter_.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    visitor_ = std::make_unique<DeferVisitor>(compiler.getASTContext(), rewriter_, outputDir_, inputRoot_);
    return std::make_unique<DeferASTConsumer>(*visitor_);
  }

private:
  void ensureIncludeInserted(const fs::path &originalPath) {
    (void)originalPath;
    if (!visitor_ || !visitor_->includeNeeded()) {
      return;
    }

    SourceManager &sourceManager = rewriter_.getSourceMgr();
    const FileID fileId = sourceManager.getMainFileID();
    const StringRef bufferData = sourceManager.getBufferData(fileId);
    if (bufferData.contains("#include \"tooling/defer/defer.h\"")) {
      return;
    }

    SourceLocation insertionLocation = sourceManager.getLocForStartOfFile(fileId);
    rewriter_.InsertText(insertionLocation, "#include \"tooling/defer/defer.h\"\n", false, true);
  }

  Rewriter rewriter_;
  fs::path outputDir_;
  fs::path inputRoot_;
  fs::path inputRootCanonical_;
  fs::path protectedSrcDir_;
  fs::path protectedLibDir_;
  std::unique_ptr<DeferVisitor> visitor_;
  bool hadWriteError_ = false;

  void initializeProtectedDirectories() {
    std::error_code ec;
    fs::path normalizedRoot = inputRoot_;

    if (normalizedRoot.empty()) {
      normalizedRoot = fs::current_path(ec);
      ec.clear();
    }

    if (!normalizedRoot.is_absolute()) {
      normalizedRoot = fs::absolute(normalizedRoot, ec);
      ec.clear();
    }

    inputRootCanonical_ = fs::weakly_canonical(normalizedRoot, ec);
    if (ec) {
      inputRootCanonical_.clear();
      return;
    }

    protectedSrcDir_ = inputRootCanonical_ / "src";
    protectedLibDir_ = inputRootCanonical_ / "lib";
  }

  static bool pathStartsWith(const fs::path &path, const fs::path &prefix) {
    if (prefix.empty()) {
      return false;
    }

    auto pathIter = path.begin();
    for (auto prefixIter = prefix.begin(); prefixIter != prefix.end(); ++prefixIter) {
      if (pathIter == path.end() || *pathIter != *prefixIter) {
        return false;
      }
      ++pathIter;
    }

    return true;
  }

  bool isInProtectedSourceTree(const fs::path &path) const {
    if (inputRootCanonical_.empty()) {
      return false;
    }

    std::error_code ec;
    fs::path canonicalPath = fs::weakly_canonical(path, ec);
    if (ec) {
      return false;
    }

    return pathStartsWith(canonicalPath, protectedSrcDir_) || pathStartsWith(canonicalPath, protectedLibDir_);
  }
};

class DeferActionFactory : public tooling::FrontendActionFactory {
public:
  DeferActionFactory(const fs::path &outputDir, const fs::path &inputRoot)
      : outputDir_(outputDir), inputRoot_(inputRoot) {}

  std::unique_ptr<FrontendAction> create() {
    return std::make_unique<DeferFrontendAction>(outputDir_, inputRoot_);
  }

private:
  fs::path outputDir_;
  fs::path inputRoot_;
};

} // namespace

int main(int argc, const char **argv) {
  InitLLVM InitLLVM(argc, argv);

  cl::ParseCommandLineOptions(argc, argv, "ascii-defer transformation tool\n");

  const fs::path outputDir = fs::path(OutputDirectoryOption.getValue());
  fs::path inputRoot;
  if (!InputRootOption.getValue().empty()) {
    inputRoot = fs::path(InputRootOption.getValue());
  } else {
    inputRoot = fs::current_path();
  }

  std::vector<std::string> sourcePaths;
  for (const auto &path : SourcePaths) {
    if (!path.empty()) {
      sourcePaths.push_back(path);
    }
  }

  if (sourcePaths.empty()) {
    llvm::errs() << "No translation units specified for transformation. Provide positional source paths.\n";
    return 1;
  }

  if (fs::exists(outputDir)) {
    if (!fs::is_directory(outputDir)) {
      llvm::errs() << "Output path exists and is not a directory: " << outputDir.c_str() << "\n";
      return 1;
    }
  } else {
    std::error_code errorCode;
    fs::create_directories(outputDir, errorCode);
    if (errorCode) {
      llvm::errs() << "Failed to create output directory: " << outputDir.c_str() << " - " << errorCode.message()
                   << "\n";
      return 1;
    }
  }

  // Load compilation database
  std::string buildPath = BuildPath.getValue();
  if (buildPath.empty()) {
    buildPath = ".";
  }
  std::string errorMessage;
  std::unique_ptr<tooling::CompilationDatabase> compilations =
      tooling::CompilationDatabase::loadFromDirectory(buildPath, errorMessage);
  if (!compilations) {
    llvm::errs() << "Error loading compilation database from '" << buildPath << "': " << errorMessage << "\n";
    return 1;
  }

  tooling::ClangTool tool(*compilations, sourcePaths);

  // Strip unnecessary flags for faster processing
  auto stripUnnecessaryFlags = [](const tooling::CommandLineArguments &args, StringRef) {
    tooling::CommandLineArguments result;
    for (size_t i = 0; i < args.size(); ++i) {
      const std::string &arg = args[i];
      if (arg.find("-fsanitize") != std::string::npos) continue;
      if (arg.find("-fno-sanitize") != std::string::npos) continue;
      if (arg == "-g" || arg == "-g2" || arg == "-g3") continue;
      result.push_back(arg);
    }
    return result;
  };
  tool.appendArgumentsAdjuster(stripUnnecessaryFlags);

  // Add ASCII_DEFER_TOOL_PARSING definition so defer() macro doesn't cause parse errors
  tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster("-DASCII_DEFER_TOOL_PARSING",
                                                                   tooling::ArgumentInsertPosition::END));

  DeferActionFactory actionFactory(outputDir, inputRoot);
  const int executionResult = tool.run(&actionFactory);
  if (executionResult != 0) {
    llvm::errs() << "Defer transformation failed with code " << executionResult << "\n";
  }
  return executionResult;
}
