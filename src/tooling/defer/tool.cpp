#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Driver/Driver.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

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
  bool inserted = registry.insert(path).second;
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

// Structure to track a block (compound statement) that contains defers
struct BlockScope {
  CompoundStmt *stmt = nullptr;
  unsigned scopeId = 0;
  unsigned depth = 0; // Nesting depth (0 = function body)
  bool hasDefers = false;
  bool endsWithReturn = false; // True if block's last statement is a return
  SourceLocation startLoc;     // After opening brace
  SourceLocation endLoc;       // Before closing brace
};

// Structure to track defer calls within a function
struct DeferCall {
  SourceLocation location;
  SourceLocation endLocation; // End of the defer statement (after semicolon)
  unsigned fileOffset;        // File offset of the defer statement
  std::string expression;     // The code to execute (e.g., "fclose(f)" or "{ cleanup(); }")
  unsigned scopeId;           // Which scope this defer belongs to
};

// Structure to track return statements and their active scopes
struct ReturnInfo {
  SourceLocation location;
  unsigned fileOffset;                  // File offset of the return statement
  std::vector<unsigned> activeScopeIds; // Scopes that are active at this return
};

// Structure to track function transformation state
struct FunctionTransformState {
  FunctionDecl *funcDecl = nullptr;
  std::vector<DeferCall> deferCalls;
  std::vector<ReturnInfo> returnInfos;
  std::map<unsigned, BlockScope> blockScopes; // scopeId -> BlockScope
  std::vector<unsigned> currentScopeStack;    // Stack of active scope IDs during traversal
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

  bool TraverseCompoundStmt(CompoundStmt *compoundStmt) {
    if (!compoundStmt || !currentFunction_.funcDecl) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseCompoundStmt(compoundStmt);
    }

    SourceManager &sourceManager = context_.getSourceManager();
    SourceLocation lbracLoc = compoundStmt->getLBracLoc();
    if (!lbracLoc.isValid() || !sourceManager.isWrittenInMainFile(lbracLoc)) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseCompoundStmt(compoundStmt);
    }

    // Create a new scope for this block
    unsigned scopeId = currentFunction_.nextScopeId++;
    unsigned depth = currentFunction_.currentScopeStack.size();

    BlockScope blockScope;
    blockScope.stmt = compoundStmt;
    blockScope.scopeId = scopeId;
    blockScope.depth = depth;
    blockScope.hasDefers = false;
    blockScope.endsWithReturn = false;
    blockScope.startLoc = compoundStmt->getLBracLoc().getLocWithOffset(1);
    blockScope.endLoc = compoundStmt->getRBracLoc();

    // Check if block ends with a return statement
    if (!compoundStmt->body_empty()) {
      Stmt *lastStmt = compoundStmt->body_back();
      if (isa<ReturnStmt>(lastStmt)) {
        blockScope.endsWithReturn = true;
      }
    }

    currentFunction_.blockScopes[scopeId] = blockScope;
    currentFunction_.currentScopeStack.push_back(scopeId);

    // Traverse children
    bool result = RecursiveASTVisitor<DeferVisitor>::TraverseCompoundStmt(compoundStmt);

    // Pop the scope
    currentFunction_.currentScopeStack.pop_back();

    return result;
  }

  bool TraverseStmt(Stmt *stmt) {
    if (!stmt || !currentFunction_.funcDecl) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseStmt(stmt);
    }

    // Skip container statements that may contain defers in nested blocks.
    // We DON'T skip DoStmt because defer() macro expands to do { ... } while(0).
    // We DO skip IfStmt/ForStmt/WhileStmt/SwitchStmt because their source text
    // includes child statements with defers that should be tracked at inner scopes.

    if (isa<CompoundStmt>(stmt) || isa<IfStmt>(stmt) || isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) ||
        isa<SwitchStmt>(stmt)) {
      return RecursiveASTVisitor<DeferVisitor>::TraverseStmt(stmt);
    }

    SourceManager &sourceManager = context_.getSourceManager();
    SourceLocation stmtLoc = stmt->getBeginLoc();

    // For macro-expanded statements, check the expansion location
    SourceLocation checkLoc = stmtLoc;
    if (stmtLoc.isMacroID()) {
      checkLoc = sourceManager.getExpansionLoc(stmtLoc);
    }

    if (checkLoc.isValid() && sourceManager.isWrittenInMainFile(checkLoc)) {
      CharSourceRange range;
      bool isMacro = stmtLoc.isMacroID();

      if (isMacro) {
        // For macro-expanded statements, get the full macro call range
        // This includes the macro name AND arguments
        CharSourceRange macroRange = sourceManager.getImmediateExpansionRange(stmtLoc);
        range = macroRange;
      } else {
        SourceLocation begin = stmt->getBeginLoc();
        SourceLocation end = stmt->getEndLoc();
        if (!begin.isValid() || !end.isValid()) {
          return RecursiveASTVisitor<DeferVisitor>::TraverseStmt(stmt);
        }
        range = CharSourceRange::getTokenRange(begin, end);
      }

      bool invalid = false;
      StringRef stmtText = Lexer::getSourceText(range, sourceManager, context_.getLangOpts(), &invalid);

      // Get begin location for defer location calculation
      SourceLocation begin = isMacro ? range.getBegin() : stmt->getBeginLoc();

      // Only process defer() for DoStmt (or non-macro statements)
      // This avoids processing the same defer multiple times for child nodes
      bool shouldProcess = !isMacro || isa<DoStmt>(stmt);

      if (shouldProcess && !invalid && stmtText.contains("defer(")) {
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

            // Calculate the end location (after the closing paren, we'll find semicolon later)
            SourceLocation deferEndLoc = begin.getLocWithOffset(closeParen + 1);

            // Get the current scope ID (innermost scope)
            unsigned currentScopeId = 0;
            if (!currentFunction_.currentScopeStack.empty()) {
              currentScopeId = currentFunction_.currentScopeStack.back();
            }

            // Store the expression as-is - we'll inline it directly at exit points
            std::string exprStr = expression.str();

            // Trim leading/trailing whitespace
            size_t firstNonSpace = exprStr.find_first_not_of(" \t\n\r");
            size_t lastNonSpace = exprStr.find_last_not_of(" \t\n\r");
            if (firstNonSpace != std::string::npos && lastNonSpace != std::string::npos) {
              exprStr = exprStr.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
            }

            DeferCall deferCall;
            deferCall.location = deferLoc;
            deferCall.endLocation = deferEndLoc;
            deferCall.fileOffset = sourceManager.getFileOffset(deferLoc);
            deferCall.expression = exprStr;
            deferCall.scopeId = currentScopeId;

            // Mark the scope as having defers
            if (currentFunction_.blockScopes.count(currentScopeId)) {
              currentFunction_.blockScopes[currentScopeId].hasDefers = true;
            }

            currentFunction_.deferCalls.push_back(deferCall);
            currentFunction_.needsTransformation = true;
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
          // Record return with its active scopes (copy the current scope stack)
          ReturnInfo returnInfo;
          returnInfo.location = expansionLocation;
          returnInfo.fileOffset = sourceManager.getFileOffset(expansionLocation);
          returnInfo.activeScopeIds = currentFunction_.currentScopeStack;
          currentFunction_.returnInfos.push_back(returnInfo);
        }
      }
    }
    return RecursiveASTVisitor<DeferVisitor>::TraverseReturnStmt(returnStmt);
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

  // Get defers for a specific scope in LIFO order (last registered first)
  std::vector<const DeferCall *> getDefersForScope(unsigned scopeId, const std::vector<DeferCall> &deferCalls) const {
    std::vector<const DeferCall *> result;
    for (const auto &dc : deferCalls) {
      if (dc.scopeId == scopeId) {
        result.push_back(&dc);
      }
    }
    // Reverse for LIFO order
    std::reverse(result.begin(), result.end());
    return result;
  }

  // Format a defer expression for inline insertion
  std::string formatDeferExpression(const std::string &expr) const {
    // Check if it's a block-style defer (starts with '{')
    if (!expr.empty() && expr[0] == '{') {
      // Block defer - execute the block directly
      return "do " + expr + " while(0); ";
    } else {
      // Function call defer - just add semicolon if needed
      std::string result = expr;
      // Trim trailing semicolons/whitespace
      while (!result.empty() && (result.back() == ';' || result.back() == ' ' || result.back() == '\t' ||
                                 result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
      }
      return result + "; ";
    }
  }

  // Generate inline cleanup code for all active scopes at a return statement (LIFO order)
  // Only includes defers that were declared BEFORE the return statement
  std::string generateInlineCleanupForReturn(const ReturnInfo &returnInfo, const FunctionTransformState &state) const {
    std::string code;
    // Process scopes from innermost to outermost
    for (auto scopeIt = returnInfo.activeScopeIds.rbegin(); scopeIt != returnInfo.activeScopeIds.rend(); ++scopeIt) {
      unsigned scopeId = *scopeIt;
      auto blockIt = state.blockScopes.find(scopeId);
      if (blockIt == state.blockScopes.end() || !blockIt->second.hasDefers) {
        continue;
      }
      // Get defers for this scope in LIFO order, but only those declared before this return
      auto defers = getDefersForScopeBeforeOffset(scopeId, state.deferCalls, returnInfo.fileOffset);
      for (const auto *dc : defers) {
        code += formatDeferExpression(dc->expression);
      }
    }
    return code;
  }

  // Get defers for a specific scope that were declared before a given file offset (in LIFO order)
  std::vector<const DeferCall *>
  getDefersForScopeBeforeOffset(unsigned scopeId, const std::vector<DeferCall> &deferCalls, unsigned maxOffset) const {
    std::vector<const DeferCall *> result;
    for (const auto &dc : deferCalls) {
      if (dc.scopeId == scopeId && dc.fileOffset < maxOffset) {
        result.push_back(&dc);
      }
    }
    // Reverse for LIFO order
    std::reverse(result.begin(), result.end());
    return result;
  }

  // Generate inline cleanup code for end of a block (LIFO order)
  std::string generateInlineCleanupAtBlockEnd(unsigned scopeId, const FunctionTransformState &state) const {
    std::string code;
    auto defers = getDefersForScope(scopeId, state.deferCalls);
    for (const auto *dc : defers) {
      code += "    " + formatDeferExpression(dc->expression) + "\n";
    }
    return code;
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

    // Step 1: Remove all defer() statements
    for (const DeferCall &deferCall : state.deferCalls) {
      removeDeferStatement(deferCall);
    }

    // Step 2: Insert cleanup before each return statement (inline the deferred code)
    for (const ReturnInfo &returnInfo : state.returnInfos) {
      std::string cleanup = generateInlineCleanupForReturn(returnInfo, state);
      if (!cleanup.empty()) {
        rewriter_.InsertText(returnInfo.location, cleanup, true, true);
      }
    }

    // Step 3: Insert cleanup at the end of each block that has defers
    // Skip blocks that end with a return statement (cleanup already inserted before the return)
    for (const auto &pair : state.blockScopes) {
      const BlockScope &blockScope = pair.second;
      if (blockScope.hasDefers && blockScope.endLoc.isValid() && !blockScope.endsWithReturn) {
        std::string cleanup = generateInlineCleanupAtBlockEnd(blockScope.scopeId, state);
        if (!cleanup.empty()) {
          rewriter_.InsertText(blockScope.endLoc, cleanup, true, true);
        }
      }
    }
  }

  void removeDeferStatement(const DeferCall &deferCall) {
    SourceManager &sourceManager = context_.getSourceManager();

    SourceLocation macroLoc = deferCall.location;
    if (!macroLoc.isValid()) {
      return;
    }

    // Get the range covering "defer(expression);"
    FileID fileId = sourceManager.getFileID(macroLoc);
    bool invalid = false;
    StringRef fileData = sourceManager.getBufferData(fileId, &invalid);
    if (invalid) {
      return;
    }

    unsigned offset = sourceManager.getFileOffset(macroLoc);

    // Find "defer(" starting at offset
    size_t deferStart = fileData.find("defer(", offset);
    if (deferStart == StringRef::npos || deferStart != offset) {
      return; // Not at the expected position
    }

    // Find matching closing paren for defer(...)
    size_t openParen = deferStart + 5; // Position of '(' in defer(
    size_t closeParen = findMatchingParenInFile(fileData, openParen);
    if (closeParen == StringRef::npos) {
      return;
    }

    // Find the semicolon AFTER the closing paren
    size_t semicolonPos = closeParen + 1;
    while (semicolonPos < fileData.size() && (fileData[semicolonPos] == ' ' || fileData[semicolonPos] == '\t' ||
                                              fileData[semicolonPos] == '\n' || fileData[semicolonPos] == '\r')) {
      semicolonPos++;
    }
    if (semicolonPos >= fileData.size() || fileData[semicolonPos] != ';') {
      return; // No semicolon found after closing paren
    }

    SourceLocation semicolonLoc = macroLoc.getLocWithOffset(semicolonPos - offset);
    CharSourceRange deferRange = CharSourceRange::getCharRange(macroLoc, semicolonLoc.getLocWithOffset(1));

    // Replace with a comment noting the defer was moved
    // For block defers, just note it's a block defer to avoid multiline comment issues
    std::string exprSummary = deferCall.expression;
    bool isBlockDefer = !exprSummary.empty() && exprSummary[0] == '{';
    if (isBlockDefer) {
      exprSummary = "{...}"; // Summarize block defers
    }
    std::string comment = "/* defer: " + exprSummary + " (moved to scope exit) */";
    rewriter_.ReplaceText(deferRange, comment);
  }

  size_t findMatchingParenInFile(StringRef fileData, size_t openPos) const {
    if (openPos >= fileData.size() || fileData[openPos] != '(') {
      return StringRef::npos;
    }

    int depth = 1;
    size_t i = openPos + 1;
    while (i < fileData.size()) {
      char c = fileData[i];
      if (c == '(') {
        depth++;
        i++;
      } else if (c == ')') {
        depth--;
        if (depth == 0) {
          return i;
        }
        i++;
      } else if (c == '"') {
        // Skip string literals to avoid counting parens inside strings
        i++;
        while (i < fileData.size() && fileData[i] != '"') {
          if (fileData[i] == '\\' && i + 1 < fileData.size()) {
            i++;  // Skip escaped character
          }
          i++;
        }
        if (i < fileData.size()) {
          i++;  // Skip closing quote
        }
      } else if (c == '\'') {
        // Skip character literals
        i++;
        while (i < fileData.size() && fileData[i] != '\'') {
          if (fileData[i] == '\\' && i + 1 < fileData.size()) {
            i++;  // Skip escaped character
          }
          i++;
        }
        if (i < fileData.size()) {
          i++;  // Skip closing quote
        }
      } else {
        i++;
      }
    }

    return StringRef::npos;
  }

  ASTContext &context_;
  Rewriter &rewriter_;
  fs::path outputDir_;
  fs::path inputRoot_;
  FunctionTransformState currentFunction_;
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

    // outputDir_ is already absolute (made absolute in main()), so destinationPath should be too

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

// Store the original CWD before any tool changes it
static fs::path g_originalCwd;

int main(int argc, const char **argv) {
  InitLLVM InitLLVM(argc, argv);

  // Capture CWD before anything else can change it
  g_originalCwd = fs::current_path();

  cl::ParseCommandLineOptions(argc, argv, "ascii-defer transformation tool\n");

  fs::path outputDir = fs::path(OutputDirectoryOption.getValue());
  // Make output directory absolute relative to original CWD
  if (!outputDir.is_absolute()) {
    outputDir = g_originalCwd / outputDir;
  }
  fs::path inputRoot;
  if (!InputRootOption.getValue().empty()) {
    inputRoot = fs::path(InputRootOption.getValue());
  } else {
    inputRoot = fs::current_path();
  }

  // Make input root absolute for reliable path computation
  if (!inputRoot.is_absolute()) {
    std::error_code ec;
    inputRoot = fs::absolute(inputRoot, ec);
    if (ec) {
      llvm::errs() << "Failed to resolve input root path: " << ec.message() << "\n";
      return 1;
    }
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

  // Build the list of arguments to prepend for system header resolution
  // LibTooling uses CC1 mode internally which has different include path handling than
  // the clang driver. We use -Xclang to pass CC1-level flags that properly configure
  // system include paths for LibTooling's CompilerInvocation.
  std::vector<std::string> prependArgs;

#ifdef CLANG_RESOURCE_DIR
  {
    const char* resourceDir = CLANG_RESOURCE_DIR;
    if (llvm::sys::fs::exists(resourceDir)) {
      // Add -resource-dir for clang internals
      prependArgs.push_back(std::string("-resource-dir=") + resourceDir);
      llvm::errs() << "Using clang resource directory: " << resourceDir << "\n";
    } else {
      llvm::errs() << "Warning: Clang resource directory does not exist: " << resourceDir << "\n";
    }
  }
#endif

#ifdef MACOS_SDK_PATH
  {
    const char* sdkPath = MACOS_SDK_PATH;
    if (llvm::sys::fs::exists(sdkPath)) {
      // Set sysroot for framework resolution
      prependArgs.push_back("-isysroot");
      prependArgs.push_back(sdkPath);
      llvm::errs() << "Using macOS SDK: " << sdkPath << "\n";
    } else {
      llvm::errs() << "Warning: macOS SDK path does not exist: " << sdkPath << "\n";
    }
  }
#endif

  // Build list of system include paths to add as -isystem paths.
  // Now that project paths use -iquote (only for "..." includes), these -isystem paths
  // will be searched for <...> includes like <stdio.h>.
  //
  // NOTE: We only add clang's builtin headers here. The SDK headers are handled by
  // -isysroot which tells clang where the SDK root is. We do NOT add ${SDK}/usr/include
  // explicitly because:
  // 1. Modern macOS SDKs have sparse /usr/include (missing stdbool.h, etc.)
  // 2. Adding it causes #include_next in clang headers to fail
  // 3. -isysroot handles SDK header paths correctly
  std::vector<std::string> appendArgs;
#ifdef CLANG_RESOURCE_DIR
  {
    std::string builtinInclude = std::string(CLANG_RESOURCE_DIR) + "/include";
    if (llvm::sys::fs::exists(builtinInclude)) {
      appendArgs.push_back("-isystem");
      appendArgs.push_back(builtinInclude);
      llvm::errs() << "Added clang builtin -isystem: " << builtinInclude << "\n";
    }
  }
#endif

  // Single consolidated argument adjuster that:
  // 1. Preserves compiler path (first arg)
  // 2. Inserts prepend args immediately after compiler (-nostdlibinc, -resource-dir, -isysroot)
  // 3. Strips unnecessary flags from remaining args
  // 4. Adds system include paths as -isystem at the end (after project -I paths)
  // 5. Adds defer tool parsing define BEFORE the "--" separator
  auto consolidatedAdjuster = [prependArgs, appendArgs](const tooling::CommandLineArguments &args, StringRef) {
    tooling::CommandLineArguments result;

    if (args.empty()) {
      return result;
    }

    // First: preserve the compiler path (first argument)
    result.push_back(args[0]);

    // Second: add the prepend args right after the compiler
    for (const auto &arg : prependArgs) {
      result.push_back(arg);
    }

    // Third: process remaining arguments, stripping unnecessary flags
    // IMPORTANT: Convert -I to -iquote for project include paths.
    // Clang include search order for <header.h>:  -I paths -> -isystem paths -> system paths
    // Clang include search order for "header.h":  -iquote paths -> -I paths -> -isystem paths -> system paths
    //
    // The problem: Project -I paths are searched BEFORE -isystem paths for <stdio.h>.
    // This causes LibTooling to look for <stdio.h> in the project's lib/ directory first.
    //
    // The fix: Convert project -I to -iquote. Then <stdio.h> skips project paths entirely
    // and finds system headers in -isystem paths instead.
    bool foundSeparator = false;
    size_t separatorIndex = 0;
    for (size_t i = 1; i < args.size(); ++i) {
      const std::string &arg = args[i];

      // When we hit "--", note its position and break
      if (arg == "--") {
        foundSeparator = true;
        separatorIndex = i;
        break;
      }

      // Skip sanitizer flags
      if (arg.find("-fsanitize") != std::string::npos)
        continue;
      if (arg.find("-fno-sanitize") != std::string::npos)
        continue;
      // Skip debug info flags (not needed for AST parsing)
      if (arg == "-g" || arg == "-g2" || arg == "-g3")
        continue;
      if (arg == "-fno-eliminate-unused-debug-types")
        continue;
      if (arg == "-fno-inline")
        continue;
      // Strip -resource-dir flags and their arguments - we added our embedded path
      if (arg == "-resource-dir") {
        ++i;
        continue;
      }
      if (arg.find("-resource-dir=") == 0)
        continue;
      // Strip -isysroot flags and their arguments - we added our embedded SDK path
      if (arg == "-isysroot") {
        ++i;
        continue;
      }
      if (arg.find("-isysroot=") == 0 || (arg.find("-isysroot") == 0 && arg.length() > 9))
        continue;

      // Convert -I to -iquote for project include paths
      // This prevents <stdio.h> from being searched in project directories
      if (arg == "-I" && i + 1 < args.size()) {
        // -I /path/to/dir (separate argument)
        result.push_back("-iquote");
        result.push_back(args[++i]);
        continue;
      }
      if (arg.find("-I") == 0 && arg.length() > 2) {
        // -I/path/to/dir (combined form)
        result.push_back("-iquote");
        result.push_back(arg.substr(2));
        continue;
      }

      result.push_back(arg);
    }

    // Fourth: add system include paths at the end (after all project -I paths)
    // Using -isystem so they're searched after -I paths
    for (const auto &arg : appendArgs) {
      result.push_back(arg);
    }

    // Fifth: add the defer tool define and separator
    result.push_back("-DASCIICHAT_DEFER_TOOL_PARSING");
    if (foundSeparator) {
      result.push_back("--");
      // Copy any remaining args after "--"
      for (size_t i = separatorIndex + 1; i < args.size(); ++i) {
        result.push_back(args[i]);
      }
    }

    return result;
  };
  tool.appendArgumentsAdjuster(consolidatedAdjuster);

  // Debug: Print the final command line arguments
  tool.appendArgumentsAdjuster([](const tooling::CommandLineArguments &args, StringRef filename) {
    llvm::errs() << "Final command for " << filename << ":\n";
    for (const auto &arg : args) {
      llvm::errs() << "  " << arg << "\n";
    }
    return args;
  });

  DeferActionFactory actionFactory(outputDir, inputRoot);
  const int executionResult = tool.run(&actionFactory);
  if (executionResult != 0) {
    llvm::errs() << "Defer transformation failed with code " << executionResult << "\n";
  }
  return executionResult;
}
