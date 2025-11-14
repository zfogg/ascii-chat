#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/RewriteBuffer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace llvm;
using namespace clang;

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

// LLVM command line options MUST be declared at file scope (not in anonymous namespace)
// to ensure proper static initialization
namespace fs = std::filesystem;

static cl::OptionCategory ToolCategory("ascii-chat instrumentation options");

static cl::extrahelp CommonHelp(tooling::CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nInstrumentation tool for ascii-chat debugging\n");

static cl::opt<std::string> OutputDirectoryOption("output-dir",
                                                  cl::desc("Directory where instrumented sources will be written"),
                                                  cl::value_desc("path"), cl::Required, cl::cat(ToolCategory));

static cl::opt<std::string>
    InputRootOption("input-root", cl::desc("Root directory of original sources (used to compute relative paths)"),
                    cl::value_desc("path"), cl::init(""), cl::cat(ToolCategory));

static cl::opt<std::string> BuildPath("p", cl::desc("Build path (directory containing compile_commands.json)"),
                                      cl::Optional, cl::cat(ToolCategory));
static cl::opt<bool> LogMacroExpansionsOption("log-macro-expansions",
                                              cl::desc("Instrument statements originating from macro expansions"),
                                              cl::init(false), cl::cat(ToolCategory));

static cl::opt<bool> LogMacroInvocationsOption(
    "log-macro-invocations",
    cl::desc("Emit a synthetic record for the macro invocation site when expansions are instrumented"), cl::init(false),
    cl::cat(ToolCategory));

static cl::opt<bool> LegacyIncludeMacroExpansionsOption(
    "include-macro-expansions",
    cl::desc("Deprecated alias for --log-macro-expansions (kept for backward compatibility)"), cl::init(false),
    cl::cat(ToolCategory), cl::Hidden);

static cl::list<std::string>
    FileIncludeFilters("filter-file", cl::desc("Only instrument files whose path contains the given substring"),
                       cl::value_desc("substring"), cl::cat(ToolCategory));

static cl::list<std::string>
    FunctionIncludeFilters("filter-function",
                           cl::desc("Only instrument functions whose name matches the given substring"),
                           cl::value_desc("substring"), cl::cat(ToolCategory));

static cl::opt<std::string>
    FileListOption("file-list", cl::desc("Path to file containing newline-delimited translation units to instrument"),
                   cl::value_desc("path"), cl::init(""), cl::cat(ToolCategory));

static cl::list<std::string> SourcePaths(cl::Positional, cl::desc("<source0> [... <sourceN>]"), cl::cat(ToolCategory));

static cl::opt<std::string> SignalHandlerAnnotation(
    "signal-handler-annotation",
    cl::desc("Annotation string used to mark functions that should be skipped (default: ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER)"),
    cl::value_desc("annotation"), cl::init("ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER"), cl::cat(ToolCategory));

constexpr unsigned kMacroFlagNone = 0U;
constexpr unsigned kMacroFlagExpansion = 1U;
constexpr unsigned kMacroFlagInvocation = 2U;

namespace {

class InstrumentationVisitor : public clang::RecursiveASTVisitor<InstrumentationVisitor> {
public:
  InstrumentationVisitor(clang::ASTContext &context, clang::Rewriter &rewriter, const fs::path &outputDir,
                         const fs::path &inputRoot, bool enableFileFilters, bool enableFunctionFilters,
                         bool logMacroInvocations, bool logMacroExpansions)
      : context_(context), rewriter_(rewriter), outputDir_(outputDir), inputRoot_(inputRoot),
        enableFileFilters_(enableFileFilters), enableFunctionFilters_(enableFunctionFilters),
        logMacroInvocations_(logMacroInvocations), logMacroExpansions_(logMacroExpansions) {}

  bool TraverseFunctionDecl(clang::FunctionDecl *funcDecl) {
    const bool previousSkipState = skipCurrentFunction_;
    currentFunction_ = funcDecl;
    skipCurrentFunction_ = shouldSkipFunction(funcDecl);

    const bool result = clang::RecursiveASTVisitor<InstrumentationVisitor>::TraverseFunctionDecl(funcDecl);

    skipCurrentFunction_ = previousSkipState;
    currentFunction_ = nullptr;
    return result;
  }

  bool VisitStmt(clang::Stmt *statement) {
    if (!statement || !currentFunction_) {
      return true;
    }

    if (!isDirectChildOfCompound(*statement)) {
      return true;
    }

    if (llvm::isa<clang::CompoundStmt>(statement) || llvm::isa<clang::NullStmt>(statement)) {
      return true;
    }

    if (skipCurrentFunction_) {
      return true;
    }

    if (!shouldInstrumentStatement(*statement)) {
      return true;
    }

    clang::SourceManager &sourceManager = context_.getSourceManager();
    const clang::LangOptions &langOptions = context_.getLangOpts();

    clang::SourceLocation beginLocation = statement->getBeginLoc();
    if (beginLocation.isInvalid()) {
      return true;
    }

    clang::SourceLocation expansionLocation = sourceManager.getExpansionLoc(beginLocation);
    if (!expansionLocation.isValid()) {
      return true;
    }

    if (!sourceManager.isWrittenInMainFile(expansionLocation)) {
      return true;
    }

    const clang::FileID fileId = sourceManager.getFileID(expansionLocation);
    const clang::FileEntry *fileEntry = sourceManager.getFileEntryForID(fileId);
    if (fileEntry == nullptr) {
      return true;
    }

    const llvm::StringRef absoluteFilePath = fileEntry->tryGetRealPathName();
    if (absoluteFilePath.empty()) {
      return true;
    }

    const fs::path filePath = fs::path(absoluteFilePath.str());
    if (enableFileFilters_ && !matchesFileFilters(filePath)) {
      return true;
    }

    const std::string relativePath = makeRelativePath(filePath);
    const std::string uniqueKey = buildUniqueKey(filePath, expansionLocation, sourceManager);
    if (!instrumentedLocations_.insert(uniqueKey).second) {
      return true;
    }

    const bool isMacroExpansion =
        sourceManager.isMacroBodyExpansion(beginLocation) || sourceManager.isMacroArgExpansion(beginLocation);

    std::string instrumentationBlock;

    if (isMacroExpansion) {
      if (logMacroInvocations_) {
        const std::optional<MacroInvocationMetadata> invocationMetadata =
            buildMacroInvocationMetadata(*statement, sourceManager, langOptions);
        if (invocationMetadata.has_value()) {
          if (macroInvocationLocations_.insert(invocationMetadata->uniqueKey).second) {
            instrumentationBlock.append(buildInstrumentationLine(invocationMetadata->relativePath,
                                                                 invocationMetadata->lineNumber,
                                                                 invocationMetadata->snippet, kMacroFlagInvocation));
          }
        }
      }

      if (logMacroExpansions_) {
        const unsigned lineNumber = sourceManager.getSpellingLineNumber(expansionLocation);
        const std::optional<std::string> snippetOpt = extractSnippet(*statement, sourceManager, langOptions);
        const llvm::StringRef snippetRef = snippetOpt ? llvm::StringRef(*snippetOpt) : llvm::StringRef("<unavailable>");
        instrumentationBlock.append(
            buildInstrumentationLine(relativePath, lineNumber, snippetRef, kMacroFlagExpansion));
      }

      if (instrumentationBlock.empty()) {
        return true;
      }
    } else {
      const unsigned lineNumber = sourceManager.getSpellingLineNumber(expansionLocation);
      const std::optional<std::string> snippetOpt = extractSnippet(*statement, sourceManager, langOptions);
      const llvm::StringRef snippetRef = snippetOpt ? llvm::StringRef(*snippetOpt) : llvm::StringRef("<unavailable>");
      instrumentationBlock.append(buildInstrumentationLine(relativePath, lineNumber, snippetRef, kMacroFlagNone));
    }

    if (instrumentationBlock.empty()) {
      return true;
    }

    rewriter_.InsertText(expansionLocation, instrumentationBlock, true, true);
    includeNeeded_ = true;
    return true;
  }

  bool includeNeeded() const {
    return includeNeeded_;
  }

  std::string buildUniqueKey(const fs::path &filePath, clang::SourceLocation location,
                             const clang::SourceManager &sourceManager) const {
    const unsigned offset = sourceManager.getFileOffset(location);
    return (filePath.string() + ":" + std::to_string(offset));
  }

  std::string buildInstrumentationLine(const std::string &relativePath, unsigned lineNumber, llvm::StringRef snippet,
                                       unsigned macroFlag) const {
    std::string escapedSnippet = escapeSnippet(snippet);
    std::string instrumentationLine;
    instrumentationLine.reserve(escapedSnippet.size() + 256);
    instrumentationLine.append("ascii_instr_log_line(\"");
    instrumentationLine.append(relativePath);
    instrumentationLine.append("\", ");
    instrumentationLine.append(std::to_string(lineNumber));
    instrumentationLine.append(", __func__, \"");
    instrumentationLine.append(escapedSnippet);
    instrumentationLine.append("\", ");
    instrumentationLine.append(std::to_string(macroFlag));
    instrumentationLine.append(");\n");
    return instrumentationLine;
  }

  struct MacroInvocationMetadata {
    std::string relativePath;
    unsigned lineNumber = 0;
    std::string snippet;
    std::string uniqueKey;
  };

  std::optional<MacroInvocationMetadata> buildMacroInvocationMetadata(const clang::Stmt &statement,
                                                                      clang::SourceManager &sourceManager,
                                                                      const clang::LangOptions &langOptions) const {
    clang::SourceLocation beginLocation = statement.getBeginLoc();
    if (!(sourceManager.isMacroBodyExpansion(beginLocation) || sourceManager.isMacroArgExpansion(beginLocation))) {
      return std::nullopt;
    }

    clang::SourceLocation callerLocation = sourceManager.getImmediateMacroCallerLoc(beginLocation);
    if (!callerLocation.isValid()) {
      return std::nullopt;
    }

    callerLocation = sourceManager.getExpansionLoc(callerLocation);
    if (!callerLocation.isValid() || !sourceManager.isWrittenInMainFile(callerLocation)) {
      return std::nullopt;
    }

    const clang::FileID callerFileId = sourceManager.getFileID(callerLocation);
    const clang::FileEntry *callerFileEntry = sourceManager.getFileEntryForID(callerFileId);
    if (callerFileEntry == nullptr) {
      return std::nullopt;
    }

    const llvm::StringRef callerPathRef = callerFileEntry->tryGetRealPathName();
    if (callerPathRef.empty()) {
      return std::nullopt;
    }

    MacroInvocationMetadata metadata;
    const fs::path callerPath = fs::path(callerPathRef.str());
    metadata.relativePath = makeRelativePath(callerPath);
    metadata.lineNumber = sourceManager.getSpellingLineNumber(callerLocation);
    metadata.uniqueKey = buildUniqueKey(callerPath, callerLocation, sourceManager);

    clang::CharSourceRange expansionRange = sourceManager.getImmediateExpansionRange(beginLocation);
    if (expansionRange.isValid()) {
      bool invalid = false;
      llvm::StringRef invocationSource =
          clang::Lexer::getSourceText(expansionRange, sourceManager, langOptions, &invalid);
      if (!invalid && !invocationSource.empty()) {
        metadata.snippet = invocationSource.str();
      }
    }

    if (metadata.snippet.empty()) {
      bool invalid = false;
      clang::CharSourceRange tokenRange = clang::CharSourceRange::getTokenRange(callerLocation, callerLocation);
      llvm::StringRef fallback = clang::Lexer::getSourceText(tokenRange, sourceManager, langOptions, &invalid);
      if (!invalid && !fallback.empty()) {
        metadata.snippet = fallback.str();
      } else {
        metadata.snippet = "<macro invocation>";
      }
    }

    return metadata;
  }

  bool matchesFileFilters(const fs::path &filePath) const {
    if (FileIncludeFilters.empty()) {
      return true;
    }
    const std::string filePathString = filePath.generic_string();
    for (const std::string &token : FileIncludeFilters) {
      if (filePathString.find(token) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  bool matchesFunctionFilters(const clang::FunctionDecl *functionDecl) const {
    if (FunctionIncludeFilters.empty()) {
      return true;
    }
    if (functionDecl == nullptr) {
      return false;
    }
    const std::string functionName = functionDecl->getNameInfo().getAsString();
    for (const std::string &token : FunctionIncludeFilters) {
      if (functionName.find(token) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  bool shouldInstrumentStatement(const clang::Stmt &statement) const {
    if (llvm::isa<clang::NullStmt>(&statement)) {
      return false;
    }
    if (llvm::isa<clang::ImplicitCastExpr>(&statement)) {
      return false;
    }
    if (llvm::isa<clang::ParenExpr>(&statement)) {
      return false;
    }

    if (enableFunctionFilters_ && !matchesFunctionFilters(currentFunction_)) {
      return false;
    }

    return true;
  }

  bool shouldSkipFunction(const clang::FunctionDecl *functionDecl) const {
    if (functionDecl == nullptr) {
      return true;
    }

    if (functionDecl->isImplicit()) {
      return true;
    }

    clang::SourceManager &sourceManager = context_.getSourceManager();
    clang::SourceLocation location = functionDecl->getLocation();
    location = sourceManager.getExpansionLoc(location);
    if (!location.isValid() || !sourceManager.isWrittenInMainFile(location)) {
      return true;
    }

    for (const clang::AnnotateAttr *annotation : functionDecl->specific_attrs<clang::AnnotateAttr>()) {
      if (!annotation) {
        continue;
      }
      if (annotation->getAnnotation() == SignalHandlerAnnotation.getValue()) {
        return true;
      }
    }

    return false;
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

  static std::string escapeSnippet(llvm::StringRef snippet) {
    std::string result;
    result.reserve(snippet.size());
    constexpr std::size_t kMaxSnippetLength = 1024;
    std::size_t processed = 0;
    for (char ch : snippet) {
      if (processed >= kMaxSnippetLength) {
        result.append("<truncated>");
        break;
      }
      processed++;
      const unsigned char uch = static_cast<unsigned char>(ch);
      switch (ch) {
      case '\\':
        result.append("\\\\");
        break;
      case '"':
        result.append("\\\"");
        break;
      case '\n':
        result.append("\\n");
        break;
      case '\r':
        result.append("\\r");
        break;
      case '\t':
        result.append("\\t");
        break;
      default:
        if (std::isprint(uch) != 0 || uch >= 0x80) {
          result.push_back(static_cast<char>(uch));
        } else {
          result.append("\\\\x");
          constexpr char hexDigits[] = "0123456789ABCDEF";
          result.push_back(hexDigits[(uch >> 4) & 0xF]);
          result.push_back(hexDigits[uch & 0xF]);
        }
        break;
      }
    }
    return result;
  }

  static std::optional<std::string> extractSnippet(const clang::Stmt &statement,
                                                   const clang::SourceManager &sourceManager,
                                                   const clang::LangOptions &langOptions) {
    clang::SourceLocation begin = statement.getBeginLoc();
    clang::SourceLocation end = statement.getEndLoc();

    if (begin.isInvalid() || end.isInvalid()) {
      return std::nullopt;
    }

    clang::SourceLocation expansionBegin = sourceManager.getExpansionLoc(begin);
    clang::SourceLocation expansionEnd = sourceManager.getExpansionLoc(end);
    if (!expansionBegin.isValid() || !expansionEnd.isValid()) {
      return std::nullopt;
    }

    clang::CharSourceRange range = clang::CharSourceRange::getTokenRange(expansionBegin, expansionEnd);
    bool invalid = false;
    llvm::StringRef text = clang::Lexer::getSourceText(range, sourceManager, langOptions, &invalid);
    if (invalid || text.empty()) {
      return std::nullopt;
    }
    return text.str();
  }

private:
  clang::ASTContext &context_;
  clang::Rewriter &rewriter_;
  fs::path outputDir_;
  fs::path inputRoot_;
  bool enableFileFilters_;
  bool enableFunctionFilters_;
  bool logMacroInvocations_;
  bool logMacroExpansions_;
  const clang::FunctionDecl *currentFunction_ = nullptr;
  bool skipCurrentFunction_ = false;
  bool includeNeeded_ = false;
  std::unordered_set<std::string> instrumentedLocations_;
  std::unordered_set<std::string> macroInvocationLocations_;

  bool isDirectChildOfCompound(const clang::Stmt &statement) const {
    clang::DynTypedNodeList parents = context_.getParents(statement);
    for (const auto &parent : parents) {
      if (parent.get<clang::CompoundStmt>()) {
        return true;
      }
    }
    return false;
  }
};

class InstrumentationASTConsumer : public clang::ASTConsumer {
public:
  explicit InstrumentationASTConsumer(InstrumentationVisitor &visitor) : visitor_(visitor) {}

  void HandleTranslationUnit(clang::ASTContext &context) override {
    visitor_.TraverseDecl(context.getTranslationUnitDecl());
  }

private:
  InstrumentationVisitor &visitor_;
};

class InstrumentationFrontendAction : public clang::ASTFrontendAction {
public:
  explicit InstrumentationFrontendAction(const fs::path &outputDir, const fs::path &inputRoot, bool enableFileFilters,
                                         bool enableFunctionFilters, bool logMacroInvocations, bool logMacroExpansions)
      : outputDir_(outputDir), inputRoot_(inputRoot), enableFileFilters_(enableFileFilters),
        enableFunctionFilters_(enableFunctionFilters), logMacroInvocations_(logMacroInvocations),
        logMacroExpansions_(logMacroExpansions) {}

  void EndSourceFileAction() override {
    clang::SourceManager &sourceManager = rewriter_.getSourceMgr();
    const clang::FileEntry *fileEntry = sourceManager.getFileEntryForID(sourceManager.getMainFileID());
    if (!fileEntry) {
      return;
    }

    if (!visitor_) {
      llvm::errs() << "Instrumentation visitor not initialized; skipping file output" << "\n";
      hadWriteError_ = true;
      return;
    }

    const llvm::StringRef filePathRef = fileEntry->tryGetRealPathName();
    if (filePathRef.empty()) {
      llvm::errs() << "Unable to resolve file path for instrumented output\n";
      return;
    }

    const fs::path originalPath = fs::path(filePathRef.str());
    const std::string relativePath = visitor_->makeRelativePath(originalPath);
    const fs::path destinationPath = outputDir_ / relativePath;
    const std::string destinationString = destinationPath.string();

    if (!registerOutputPath(destinationString)) {
      return;
    }

    if (fs::exists(destinationPath)) {
      llvm::errs() << "Refusing to overwrite existing file: " << destinationPath.c_str() << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
      return;
    }

    const fs::path parent = destinationPath.parent_path();
    std::error_code directoryError;
    fs::create_directories(parent, directoryError);
    if (directoryError) {
      llvm::errs() << "Failed to create output directory: " << parent.c_str() << " - " << directoryError.message()
                   << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
      return;
    }

    ensureIncludeInserted(originalPath);

    std::string rewrittenContents;
    if (const llvm::RewriteBuffer *buffer = rewriter_.getRewriteBufferFor(sourceManager.getMainFileID())) {
      rewrittenContents.assign(buffer->begin(), buffer->end());
    } else {
      rewrittenContents = sourceManager.getBufferData(sourceManager.getMainFileID()).str();
    }

    std::error_code fileError;
    llvm::raw_fd_ostream outputStream(destinationPath.string(), fileError, llvm::sys::fs::OF_Text);
    if (fileError) {
      llvm::errs() << "Failed to open output file: " << destinationPath.c_str() << " - " << fileError.message() << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
      return;
    }
    outputStream << rewrittenContents;
    outputStream.close();
    if (outputStream.has_error()) {
      llvm::errs() << "Error while writing instrumented file: " << destinationPath.c_str() << "\n";
      unregisterOutputPath(destinationString);
      hadWriteError_ = true;
    }
  }

  bool hadWriteError() const {
    return hadWriteError_;
  }

protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef) override {
    rewriter_.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    visitor_ = std::make_unique<InstrumentationVisitor>(compiler.getASTContext(), rewriter_, outputDir_, inputRoot_,
                                                        enableFileFilters_, enableFunctionFilters_,
                                                        logMacroInvocations_, logMacroExpansions_);
    return std::make_unique<InstrumentationASTConsumer>(*visitor_);
  }

private:
  void ensureIncludeInserted(const fs::path &originalPath) {
    (void)originalPath; // Unused parameter - kept for future use
    if (!visitor_ || !visitor_->includeNeeded()) {
      return;
    }

    clang::SourceManager &sourceManager = rewriter_.getSourceMgr();
    const clang::FileID fileId = sourceManager.getMainFileID();
    const llvm::StringRef bufferData = sourceManager.getBufferData(fileId);
    if (bufferData.contains("#include \"tooling/source_print/instrument_log.h\"")) {
      return;
    }

    clang::SourceLocation insertionLocation = sourceManager.getLocForStartOfFile(fileId);
    rewriter_.InsertText(insertionLocation, "#include \"tooling/source_print/instrument_log.h\"\n", false, true);
  }

  clang::Rewriter rewriter_;
  fs::path outputDir_;
  fs::path inputRoot_;
  bool enableFileFilters_;
  bool enableFunctionFilters_;
  bool logMacroInvocations_;
  bool logMacroExpansions_;
  std::unique_ptr<InstrumentationVisitor> visitor_;
  bool hadWriteError_ = false;
};

class InstrumentationActionFactory : public clang::tooling::FrontendActionFactory {
public:
  InstrumentationActionFactory(const fs::path &outputDir, const fs::path &inputRoot, bool enableFileFilters,
                               bool enableFunctionFilters, bool logMacroInvocations, bool logMacroExpansions)
      : outputDir_(outputDir), inputRoot_(inputRoot), enableFileFilters_(enableFileFilters),
        enableFunctionFilters_(enableFunctionFilters), logMacroInvocations_(logMacroInvocations),
        logMacroExpansions_(logMacroExpansions) {}

  std::unique_ptr<clang::FrontendAction> create() {
    return std::make_unique<InstrumentationFrontendAction>(
        outputDir_, inputRoot_, enableFileFilters_, enableFunctionFilters_, logMacroInvocations_, logMacroExpansions_);
  }

private:
  fs::path outputDir_;
  fs::path inputRoot_;
  bool enableFileFilters_;
  bool enableFunctionFilters_;
  bool logMacroInvocations_;
  bool logMacroExpansions_;
};

} // namespace

int main(int argc, const char **argv) {
  // Initialize LLVM infrastructure (this triggers command-line option registration!)
  llvm::InitLLVM InitLLVM(argc, argv);

  // Parse command-line options
  cl::ParseCommandLineOptions(argc, argv, "ascii-chat instrumentation tool\n");

  const fs::path outputDir = fs::path(OutputDirectoryOption.getValue());
  fs::path inputRoot;
  if (!InputRootOption.getValue().empty()) {
    inputRoot = fs::path(InputRootOption.getValue());
  } else {
    inputRoot = fs::current_path();
  }

  std::vector<std::string> rawSourceArgs;
  rawSourceArgs.reserve(SourcePaths.size());
  for (const auto &path : SourcePaths) {
    rawSourceArgs.push_back(path);
  }
  std::vector<std::string> sourcePaths;
  std::vector<std::string> extraCompilerArgs;
  for (std::size_t i = 0; i < rawSourceArgs.size(); ++i) {
    const std::string &entry = rawSourceArgs[i];
    if (entry.empty()) {
      continue;
    }
    if (entry == "--") {
      continue;
    }
    if (entry[0] == '-') {
      extraCompilerArgs.push_back(entry);
      const bool consumesNext =
          (entry == "-I" || entry == "-isystem" || entry == "-include" || entry == "-include-pch" ||
           entry == "-imacros" || entry == "-idirafter" || entry == "-iprefix" || entry == "-iwithprefix" ||
           entry == "-iwithprefixbefore" || entry == "-resource-dir" || entry == "-Xclang" || entry == "-Xpreprocessor");
      if (consumesNext && (i + 1) < rawSourceArgs.size()) {
        extraCompilerArgs.push_back(rawSourceArgs[i + 1]);
        ++i;
      }
      continue;
    }
    sourcePaths.push_back(entry);
  }
  if (!FileListOption.getValue().empty()) {
    std::ifstream listStream(FileListOption.getValue());
    if (!listStream.is_open()) {
      llvm::errs() << "Failed to open file list: " << FileListOption.getValue() << "\n";
      return 1;
    }

    std::string line;
    while (std::getline(listStream, line)) {
      llvm::StringRef trimmed(line);
      trimmed = trimmed.trim();
      if (trimmed.empty()) {
        continue;
      }
      sourcePaths.emplace_back(trimmed.str());
    }
  }

  if (sourcePaths.empty()) {
    llvm::errs()
        << "No translation units specified for instrumentation. Provide positional source paths or --file-list."
        << "\n";
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

  const bool logMacroExpansions = LogMacroExpansionsOption.getValue() || LegacyIncludeMacroExpansionsOption.getValue();
  const bool logMacroInvocations = LogMacroInvocationsOption.getValue();

  if (!LogMacroExpansionsOption.getValue() && LegacyIncludeMacroExpansionsOption.getValue()) {
    llvm::errs() << "warning: --include-macro-expansions is deprecated; use --log-macro-expansions instead\n";
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

  clang::tooling::ClangTool tool(*compilations, sourcePaths);

  if (!extraCompilerArgs.empty()) {
    const std::vector<std::string> extraArgsCopy(extraCompilerArgs.begin(), extraCompilerArgs.end());
    tool.appendArgumentsAdjuster(
        [extraArgsCopy](const clang::tooling::CommandLineArguments &args, llvm::StringRef) {
          clang::tooling::CommandLineArguments adjusted = args;
          adjusted.insert(adjusted.end(), extraArgsCopy.begin(), extraArgsCopy.end());
          return adjusted;
        });
  }

  auto stripPchAdjuster = [](const clang::tooling::CommandLineArguments &args, llvm::StringRef) {
    clang::tooling::CommandLineArguments result;
    const auto containsCMakePch = [](llvm::StringRef value) { return value.contains("cmake_pch"); };

    for (std::size_t i = 0; i < args.size(); ++i) {
      const std::string &arg = args[i];
      llvm::StringRef argRef(arg);

      if ((argRef == "-include" || argRef == "--include" || argRef == "-include-pch" || argRef == "--include-pch") &&
          (i + 1) < args.size()) {
        if (containsCMakePch(args[i + 1])) {
          ++i;
          continue;
        }
      }

      if ((argRef.starts_with("-include=") || argRef.starts_with("--include=") || argRef.starts_with("-include-pch=") ||
           argRef.starts_with("--include-pch=")) &&
          containsCMakePch(argRef)) {
        continue;
      }

      result.push_back(arg);
    }

    return result;
  };
  tool.appendArgumentsAdjuster(stripPchAdjuster);

  auto ensureSourceIncludeAdjuster = [&inputRoot](const clang::tooling::CommandLineArguments &args, llvm::StringRef) {
    clang::tooling::CommandLineArguments result = args;

    const auto hasInclude = [&result](const std::string &dir) {
      const std::string combined = "-I" + dir;
      if (std::find(result.begin(), result.end(), combined) != result.end()) {
        return true;
      }
      for (std::size_t i = 0; i + 1 < result.size(); ++i) {
        if (result[i] == "-I" && result[i + 1] == dir) {
          return true;
        }
      }
      return false;
    };

    const auto appendInclude = [&result](const std::string &dir) {
      result.push_back("-I");
      result.push_back(dir);
    };

    const std::string libDir = (inputRoot / "lib").generic_string();
    const std::string srcDir = (inputRoot / "src").generic_string();

    if (!hasInclude(libDir)) {
      appendInclude(libDir);
    }
    if (!hasInclude(srcDir)) {
      appendInclude(srcDir);
    }

    return result;
  };
  tool.appendArgumentsAdjuster(ensureSourceIncludeAdjuster);

  // Instrumented headers are now hard-linked into the output tree, so we can
  // rely on the original include paths without further adjustment.

  // NOTE: -skip-function-bodies optimization removed - it was causing errors with LibTooling
  // LibTooling handles arguments differently than the clang driver, so -Xclang flags don't work
  // The optimization wasn't providing significant benefit anyway

  // Optimization: strip unnecessary compilation flags
  auto stripUnnecessaryFlags = [](const clang::tooling::CommandLineArguments &args, llvm::StringRef) {
    clang::tooling::CommandLineArguments result;

    for (size_t i = 0; i < args.size(); ++i) {
      const std::string &arg = args[i];

      // Skip sanitizer flags (not needed for instrumentation, slow down parsing)
      if (arg.find("-fsanitize") != std::string::npos) continue;
      if (arg.find("-fno-sanitize") != std::string::npos) continue;
      if (arg.find("sanitize") != std::string::npos) continue;

      // Skip debug info generation flags (not needed, slow down codegen)
      if (arg == "-g" || arg == "-g2" || arg == "-g3") continue;
      if (arg == "-gcolumn-info") continue;
      if (arg == "-fstandalone-debug") continue;
      if (arg.find("-gcodeview") != std::string::npos) continue;
      if (arg.find("-gdwarf") != std::string::npos) continue;

      // Skip stack protector (not needed for instrumentation)
      if (arg.find("-fstack-protector") != std::string::npos) continue;

      // Skip frame pointer flags
      if (arg.find("-fno-omit-frame-pointer") != std::string::npos) continue;
      if (arg.find("-fomit-frame-pointer") != std::string::npos) continue;

      // Skip optimization-related debug flags
      if (arg == "-fno-inline") continue;
      if (arg == "-fno-eliminate-unused-debug-types") continue;

      result.push_back(arg);
    }

    // NOTE: -w flag removed - it was causing "no such file or directory" errors with LibTooling
    // Warnings are already suppressed by the stripped flags above

    return result;
  };
  tool.appendArgumentsAdjuster(stripUnnecessaryFlags);

  InstrumentationActionFactory actionFactory(outputDir, inputRoot, !FileIncludeFilters.empty(),
                                             !FunctionIncludeFilters.empty(), logMacroInvocations, logMacroExpansions);
  const int executionResult = tool.run(&actionFactory);
  if (executionResult != 0) {
    llvm::errs() << "Instrumenter failed with code " << executionResult << "\n";
  }
  return executionResult;
}
