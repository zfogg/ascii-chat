/**
 * @file lldb_controller.h
 * @brief LLDB process attachment and control wrapper
 *
 * This class wraps the LLDB SB API to provide process attachment, control,
 * and variable inspection. It's designed for external debugging - the
 * controller runs as a separate process from the target.
 *
 * Thread Safety:
 *   Most LLDB operations are NOT thread-safe. This class assumes single-threaded
 *   use from the HTTP server's request handler thread.
 */

#pragma once

#include <lldb/API/LLDB.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ascii_query {

/**
 * @brief Process state enumeration
 */
enum class ProcessState {
  Invalid,  ///< No valid process attached
  Running,  ///< Process is running normally
  Stopped,  ///< Process is stopped (breakpoint, signal, etc.)
  Exited,   ///< Process has exited
  Crashed,  ///< Process crashed
  Detached, ///< Detached from process
};

/**
 * @brief Thread information
 */
struct ThreadInfo {
  uint64_t id;             ///< Thread ID
  uint32_t index;          ///< Index in thread list
  std::string name;        ///< Thread name (may be empty)
  std::string stop_reason; ///< Why thread is stopped (if stopped)
  std::string function;    ///< Current function name
  std::string file;        ///< Current source file
  uint32_t line;           ///< Current line number
  bool is_selected;        ///< Is this the selected thread?
};

/**
 * @brief Stack frame information
 */
struct FrameInfo {
  uint32_t index;       ///< Frame index (0 = innermost)
  std::string function; ///< Function name
  std::string file;     ///< Source file
  uint32_t line;        ///< Line number
  uint64_t pc;          ///< Program counter
  uint64_t fp;          ///< Frame pointer
};

/**
 * @brief Variable information (from LLDB SBValue)
 */
struct VariableInfo {
  std::string name;    ///< Variable name
  std::string type;    ///< Type name
  std::string value;   ///< Value as string
  std::string summary; ///< LLDB summary (for complex types)
  uint64_t address;    ///< Memory address
  size_t size;         ///< Size in bytes
  bool is_valid;       ///< Whether value could be read
  bool is_pointer;     ///< Whether this is a pointer type
  bool is_aggregate;   ///< Whether this is struct/class/array

  std::vector<VariableInfo> children; ///< Struct members / array elements
};

/**
 * @brief Breakpoint information
 */
struct BreakpointInfo {
  int32_t id;            ///< LLDB breakpoint ID
  std::string file;      ///< Source file
  uint32_t line;         ///< Line number
  std::string condition; ///< Condition expression (may be empty)
  uint32_t hit_count;    ///< Number of times hit
  bool enabled;          ///< Is breakpoint enabled?
  bool resolved;         ///< Is breakpoint resolved (has valid location)?
};

/**
 * @brief LLDB process controller
 *
 * Wraps LLDB's SB API to provide process attachment, breakpoint management,
 * and variable inspection for the query tool.
 */
class LLDBController {
public:
  LLDBController();
  ~LLDBController();

  // Non-copyable, non-movable (owns LLDB resources)
  LLDBController(const LLDBController &) = delete;
  LLDBController &operator=(const LLDBController &) = delete;
  LLDBController(LLDBController &&) = delete;
  LLDBController &operator=(LLDBController &&) = delete;

  // =========================================================================
  // Initialization
  // =========================================================================

  /**
   * @brief Initialize LLDB. Must be called before any other methods.
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * @brief Shutdown LLDB and release resources.
   */
  void shutdown();

  // =========================================================================
  // Process Attachment
  // =========================================================================

  /**
   * @brief Attach to a process by PID
   * @param pid Process ID to attach to
   * @return true on success, false on failure
   *
   * On success, the target process is stopped.
   */
  bool attach(pid_t pid);

  /**
   * @brief Attach to a process by name
   * @param process_name Name of process to attach to
   * @param wait_for If true, wait for process to start
   * @return true on success, false on failure
   */
  bool attachByName(const std::string &process_name, bool wait_for = false);

  /**
   * @brief Detach from the current process
   *
   * The target process continues running after detach.
   */
  void detach();

  /**
   * @brief Check if attached to a process
   * @return true if attached, false otherwise
   */
  [[nodiscard]] bool isAttached() const;

  /**
   * @brief Get the PID of the attached process
   * @return PID, or 0 if not attached
   */
  [[nodiscard]] pid_t targetPid() const;

  /**
   * @brief Get the name of the attached process
   * @return Process name, or empty string if not attached
   */
  [[nodiscard]] std::string targetName() const;

  // =========================================================================
  // Process Control
  // =========================================================================

  /**
   * @brief Stop the target process
   * @return true if stopped successfully
   */
  bool stop();

  /**
   * @brief Resume the target process
   * @return true if resumed successfully
   */
  bool resume();

  /**
   * @brief Single step the current thread (step into)
   * @return true if step completed
   */
  bool stepInto();

  /**
   * @brief Step over the current line
   * @return true if step completed
   */
  bool stepOver();

  /**
   * @brief Step out of the current function
   * @return true if step completed
   */
  bool stepOut();

  /**
   * @brief Get the current process state
   * @return ProcessState enum value
   */
  [[nodiscard]] ProcessState state() const;

  /**
   * @brief Get the last error message
   * @return Error message string
   */
  [[nodiscard]] const std::string &lastError() const;

  // =========================================================================
  // Thread Information
  // =========================================================================

  /**
   * @brief Get list of all threads
   * @return Vector of ThreadInfo structs
   */
  [[nodiscard]] std::vector<ThreadInfo> getThreads() const;

  /**
   * @brief Get the currently selected thread
   * @return ThreadInfo or nullopt if no thread selected
   */
  [[nodiscard]] std::optional<ThreadInfo> getSelectedThread() const;

  /**
   * @brief Select a thread by ID
   * @param thread_id Thread ID to select
   * @return true if thread was found and selected
   */
  bool selectThread(uint64_t thread_id);

  // =========================================================================
  // Stack Frames
  // =========================================================================

  /**
   * @brief Get stack frames for the selected thread
   * @param max_frames Maximum number of frames to return (0 = all)
   * @return Vector of FrameInfo structs
   */
  [[nodiscard]] std::vector<FrameInfo> getFrames(uint32_t max_frames = 0) const;

  /**
   * @brief Get a specific frame by index
   * @param frame_index Frame index (0 = innermost)
   * @return FrameInfo or nullopt if index out of range
   */
  [[nodiscard]] std::optional<FrameInfo> getFrame(uint32_t frame_index) const;

  // =========================================================================
  // Variable Reading
  // =========================================================================

  /**
   * @brief Read a variable from the current frame
   * @param name Variable name (supports dot notation for members)
   * @param frame_index Which frame to read from (0 = current)
   * @param expand_depth How deep to expand struct members (0 = don't expand)
   * @return VariableInfo or nullopt if not found
   */
  [[nodiscard]] std::optional<VariableInfo> readVariable(const std::string &name, uint32_t frame_index = 0,
                                                         uint32_t expand_depth = 0) const;

  /**
   * @brief List all variables in scope at a frame
   * @param frame_index Which frame (0 = current)
   * @param include_args Include function arguments
   * @param include_locals Include local variables
   * @param include_statics Include static variables
   * @return Vector of VariableInfo structs
   */
  [[nodiscard]] std::vector<VariableInfo> listVariables(uint32_t frame_index = 0, bool include_args = true,
                                                        bool include_locals = true, bool include_statics = false) const;

  // =========================================================================
  // Breakpoints
  // =========================================================================

  /**
   * @brief Set a breakpoint at file:line
   * @param file Source file path
   * @param line Line number
   * @param condition Optional condition expression
   * @return Breakpoint ID, or -1 on failure
   */
  int32_t setBreakpoint(const std::string &file, uint32_t line, const std::string &condition = "");

  /**
   * @brief Remove a breakpoint
   * @param breakpoint_id Breakpoint ID to remove
   * @return true if breakpoint was found and removed
   */
  bool removeBreakpoint(int32_t breakpoint_id);

  /**
   * @brief Get all breakpoints
   * @return Vector of BreakpointInfo structs
   */
  [[nodiscard]] std::vector<BreakpointInfo> getBreakpoints() const;

  /**
   * @brief Get information about a specific breakpoint
   * @param breakpoint_id Breakpoint ID
   * @return BreakpointInfo or nullopt if not found
   */
  [[nodiscard]] std::optional<BreakpointInfo> getBreakpoint(int32_t breakpoint_id) const;

  /**
   * @brief Wait for any breakpoint to be hit
   * @param timeout_ms Timeout in milliseconds (0 = no timeout)
   * @return true if breakpoint was hit, false on timeout
   */
  bool waitForBreakpoint(uint32_t timeout_ms = 0);

  // =========================================================================
  // Expression Evaluation
  // =========================================================================

  /**
   * @brief Evaluate an expression in the current context
   * @param expression Expression to evaluate
   * @param frame_index Which frame context (0 = current)
   * @return VariableInfo with result, or nullopt on failure
   */
  [[nodiscard]] std::optional<VariableInfo> evaluateExpression(const std::string &expression,
                                                               uint32_t frame_index = 0) const;

private:
  // LLDB objects (mutable because LLDB's SB API methods aren't const-correct)
  mutable lldb::SBDebugger debugger_;
  mutable lldb::SBTarget target_;
  mutable lldb::SBProcess process_;
  mutable lldb::SBListener listener_;

  // State
  bool initialized_ = false;
  mutable std::string last_error_;

  // Helper methods
  void setError(const std::string &msg) const;
  void clearError() const;

  [[nodiscard]] lldb::SBThread getSelectedThreadInternal() const;
  [[nodiscard]] lldb::SBFrame getFrameInternal(uint32_t index) const;

  // Note: LLDB SB API methods are not const in LLVM 21+, so we use non-const references
  [[nodiscard]] static ThreadInfo threadToInfo(lldb::SBThread &thread, bool is_selected);
  [[nodiscard]] static FrameInfo frameToInfo(lldb::SBFrame &frame);
  [[nodiscard]] VariableInfo valueToInfo(lldb::SBValue value, uint32_t expand_depth) const;
  [[nodiscard]] static BreakpointInfo breakpointToInfo(lldb::SBBreakpoint bp);
};

} // namespace ascii_query
