#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

static llvm::cl::OptionCategory ToolCategory("ascii-chat instrumentation options");

static llvm::cl::opt<std::string>
    OutputDirectoryOption("output-dir", llvm::cl::desc("Directory where instrumented sources will be written"),
                          llvm::cl::value_desc("path"), llvm::cl::Required, llvm::cl::cat(ToolCategory));

static llvm::cl::opt<std::string>
    InputRootOption("input-root", llvm::cl::desc("Root directory of original sources (used to compute relative paths)"),
                    llvm::cl::value_desc("path"), llvm::cl::init(""), llvm::cl::cat(ToolCategory));

static llvm::cl::list<std::string>
    FileIncludeFilters("filter-file", llvm::cl::desc("Only instrument files whose path contains the given substring"),
                       llvm::cl::value_desc("substring"), llvm::cl::cat(ToolCategory));

static llvm::cl::list<std::string>
    FunctionIncludeFilters("filter-function",
                           llvm::cl::desc("Only instrument functions whose name matches the given substring"),
                           llvm::cl::value_desc("substring"), llvm::cl::cat(ToolCategory));

static llvm::cl::opt<std::string>
    FileListOption("file-list",
                   llvm::cl::desc("Path to file containing newline-delimited translation units to instrument"),
                   llvm::cl::value_desc("path"), llvm::cl::init(""), llvm::cl::cat(ToolCategory));

static llvm::cl::opt<std::string> SignalHandlerAnnotation(
    "signal-handler-annotation",
    llvm::cl::desc(
        "Annotation string used to mark functions that should be skipped (default: ASCII_INSTR_SIGNAL_HANDLER)"),
    llvm::cl::value_desc("annotation"), llvm::cl::init("ASCII_INSTR_SIGNAL_HANDLER"), llvm::cl::cat(ToolCategory));

class InstrumentationVisitor : public clang::RecursiveASTVisitor<InstrumentationVisitor> {
public:
  InstrumentationVisitor(clang::ASTContext &context, clang::Rewriter &rewriter, const fs::path &outputDir,
                         const fs::path &inputRoot, bool enableFileFilters, bool enableFunctionFilters)
      : context_(context), rewriter_(rewriter), outputDir_(outputDir), inputRoot_(inputRoot),
        enableFileFilters_(enableFileFilters), enableFunctionFilters_(enableFunctionFilters) {}

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
    if (!statement) {
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

    const std::string uniqueKey = buildUniqueKey(filePath, expansionLocation, sourceManager);
    if (!instrumentedLocations_.insert(uniqueKey).second) {
      return true;
    }

    const bool isMacroExpansion =
        sourceManager.isMacroBodyExpansion(beginLocation) || sourceManager.isMacroArgExpansion(beginLocation);

    llvm::Optional<std::string> snippet = extractSnippet(*statement, sourceManager, langOptions);
    if (!snippet) {
      snippet = std::string("<unavailable>");
    }

    const unsigned lineNumber = sourceManager.getSpellingLineNumber(expansionLocation);
    const std::string relativePath = makeRelativePath(filePath);

    std::string escapedSnippet = escapeSnippet(*snippet);

    std::string instrumentationLine;
    instrumentationLine.reserve(escapedSnippet.size() + 256);
    instrumentationLine.append("ascii_instr_log_line(\"");
    instrumentationLine.append(relativePath);
    instrumentationLine.append("\", ");
    instrumentationLine.append(std::to_string(lineNumber));
    instrumentationLine.append(", __func__, \"");
    instrumentationLine.append(escapedSnippet);
    instrumentationLine.append("\", ");
    instrumentationLine.append(isMacroExpansion ? "1" : "0");
    instrumentationLine.append(");\n");

    const clang::SourceLocation insertLocation = expansionLocation;
    const bool insertBefore = true;
    const bool indentNewLines = true;
    rewriter_.InsertText(insertLocation, instrumentationLine, insertBefore, indentNewLines);
    includeNeeded_ = true;
    return true;
  }

  bool includeNeeded() const {
    return includeNeeded_;
  }

  std::string buildUniqueKey(const fs::path &filePath, clang::SourceLocation location,
                             const clang::SourceManager &sourceManager) {
    const unsigned offset = sourceManager.getFileOffset(location);
    return (filePath.string() + ":" + std::to_string(offset));
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

    if (!matchesFunctionFilters(currentFunction_)) {
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
        if (std::isprint(static_cast<unsigned char>(ch)) != 0) {
          result.push_back(ch);
        } else {
          result.append("\\x");
          constexpr char hexDigits[] = "0123456789ABCDEF";
          result.push_back(hexDigits[(static_cast<unsigned char>(ch) >> 4) & 0xF]);
          result.push_back(hexDigits[static_cast<unsigned char>(ch) & 0xF]);
        }
        break;
      }
    }
    return result;
  }

  static llvm::Optional<std::string> extractSnippet(const clang::Stmt &statement,
                                                    const clang::SourceManager &sourceManager,
                                                    const clang::LangOptions &langOptions) {
    clang::SourceLocation begin = statement.getBeginLoc();
    clang::SourceLocation end = statement.getEndLoc();

    if (begin.isInvalid() || end.isInvalid()) {
      return llvm::None;
    }

    clang::SourceLocation expansionBegin = sourceManager.getExpansionLoc(begin);
    clang::SourceLocation expansionEnd = sourceManager.getExpansionLoc(end);
    if (!expansionBegin.isValid() || !expansionEnd.isValid()) {
      return llvm::None;
    }

    clang::CharSourceRange range = clang::CharSourceRange::getTokenRange(expansionBegin, expansionEnd);
    bool invalid = false;
    llvm::StringRef text = clang::Lexer::getSourceText(range, sourceManager, langOptions, &invalid);
    if (invalid || text.empty()) {
      return llvm::None;
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
  const clang::FunctionDecl *currentFunction_ = nullptr;
  bool skipCurrentFunction_ = false;
  bool includeNeeded_ = false;
  std::unordered_set<std::string> instrumentedLocations_;
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
                                         bool enableFunctionFilters)
      : outputDir_(outputDir), inputRoot_(inputRoot), enableFileFilters_(enableFileFilters),
        enableFunctionFilters_(enableFunctionFilters) {}

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

    if (fs::exists(destinationPath)) {
      llvm::errs() << "Refusing to overwrite existing file: " << destinationPath.c_str() << "\n";
      hadWriteError_ = true;
      return;
    }

    const fs::path parent = destinationPath.parent_path();
    std::error_code directoryError;
    fs::create_directories(parent, directoryError);
    if (directoryError) {
      llvm::errs() << "Failed to create output directory: " << parent.c_str() << " - " << directoryError.message()
                   << "\n";
      hadWriteError_ = true;
      return;
    }

    ensureIncludeInserted(originalPath);

    std::string rewrittenContents;
    if (const clang::RewriteBuffer *buffer = rewriter_.getRewriteBufferFor(sourceManager.getMainFileID())) {
      rewrittenContents.assign(buffer->begin(), buffer->end());
    } else {
      rewrittenContents = sourceManager.getBufferData(sourceManager.getMainFileID()).str();
    }

    std::error_code fileError;
    llvm::raw_fd_ostream outputStream(destinationPath.string(), fileError, llvm::sys::fs::OF_Text);
    if (fileError) {
      llvm::errs() << "Failed to open output file: " << destinationPath.c_str() << " - " << fileError.message() << "\n";
      hadWriteError_ = true;
      return;
    }
    outputStream << rewrittenContents;
    outputStream.close();
  }

  bool hadWriteError() const {
    return hadWriteError_;
  }

protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef) override {
    rewriter_.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    visitor_ = std::make_unique<InstrumentationVisitor>(compiler.getASTContext(), rewriter_, outputDir_, inputRoot_,
                                                        enableFileFilters_, enableFunctionFilters_);
    return std::make_unique<InstrumentationASTConsumer>(*visitor_);
  }

private:
  void ensureIncludeInserted(const fs::path &originalPath) {
    if (!visitor_ || !visitor_->includeNeeded()) {
      return;
    }

    clang::SourceManager &sourceManager = rewriter_.getSourceMgr();
    const clang::FileID fileId = sourceManager.getMainFileID();
    const llvm::StringRef bufferData = sourceManager.getBufferData(fileId);
    if (bufferData.contains("#include \"debug/instrument_log.h\"")) {
      return;
    }

    clang::SourceLocation insertionLocation = sourceManager.getLocForStartOfFile(fileId);
    rewriter_.InsertText(insertionLocation, "#include \"debug/instrument_log.h\"\n", false, true);
  }

  clang::Rewriter rewriter_;
  fs::path outputDir_;
  fs::path inputRoot_;
  bool enableFileFilters_;
  bool enableFunctionFilters_;
  std::unique_ptr<InstrumentationVisitor> visitor_;
  bool hadWriteError_ = false;
};

class InstrumentationActionFactory : public clang::tooling::FrontendActionFactory {
public:
  InstrumentationActionFactory(const fs::path &outputDir, const fs::path &inputRoot, bool enableFileFilters,
                               bool enableFunctionFilters)
      : outputDir_(outputDir), inputRoot_(inputRoot), enableFileFilters_(enableFileFilters),
        enableFunctionFilters_(enableFunctionFilters) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<InstrumentationFrontendAction>(outputDir_, inputRoot_, enableFileFilters_,
                                                           enableFunctionFilters_);
  }

private:
  fs::path outputDir_;
  fs::path inputRoot_;
  bool enableFileFilters_;
  bool enableFunctionFilters_;
};

} // namespace

int main(int argc, const char **argv) {
  llvm::Expected<clang::tooling::CommonOptionsParser> optionsParserOrError =
      clang::tooling::CommonOptionsParser::create(argc, argv, ToolCategory);
  if (!optionsParserOrError) {
    llvm::errs() << optionsParserOrError.takeError();
    return 1;
  }

  clang::tooling::CommonOptionsParser &optionsParser = *optionsParserOrError;
  const fs::path outputDir = fs::path(OutputDirectoryOption.getValue());
  fs::path inputRoot;
  if (!InputRootOption.getValue().empty()) {
    inputRoot = fs::path(InputRootOption.getValue());
  } else {
    inputRoot = fs::current_path();
  }

  std::vector<std::string> sourcePaths = optionsParser.getSourcePathList();
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
    if (!fs::is_empty(outputDir)) {
      llvm::errs() << "Output directory is not empty: " << outputDir.c_str() << "\n";
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

  clang::tooling::ClangTool tool(optionsParser.getCompilations(), sourcePaths);

  InstrumentationActionFactory actionFactory(outputDir, inputRoot, !FileIncludeFilters.empty(),
                                             !FunctionIncludeFilters.empty());
  const int executionResult = tool.run(&actionFactory);
  if (executionResult != 0) {
    llvm::errs() << "Instrumenter failed with code " << executionResult << "\n";
  }
  return executionResult;
}
