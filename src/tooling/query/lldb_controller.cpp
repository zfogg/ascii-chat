/**
 * @file lldb_controller.cpp
 * @brief LLDB process attachment and control implementation
 */

#include "lldb_controller.h"

#include <lldb/API/SBBreakpoint.h>
#include <lldb/API/SBBreakpointLocation.h>
#include <lldb/API/SBCommandInterpreter.h>
#include <lldb/API/SBDebugger.h>
#include <lldb/API/SBError.h>
#include <lldb/API/SBEvent.h>
#include <lldb/API/SBFileSpec.h>
#include <lldb/API/SBFrame.h>
#include <lldb/API/SBListener.h>
#include <lldb/API/SBProcess.h>
#include <lldb/API/SBStream.h>
#include <lldb/API/SBTarget.h>
#include <lldb/API/SBThread.h>
#include <lldb/API/SBType.h>
#include <lldb/API/SBValue.h>
#include <lldb/API/SBValueList.h>

#include <chrono>
#include <cstdio>

namespace ascii_query {

// =============================================================================
// Constructor / Destructor
// =============================================================================

LLDBController::LLDBController() = default;

LLDBController::~LLDBController() { shutdown(); }

// =============================================================================
// Initialization
// =============================================================================

bool LLDBController::initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize LLDB
    lldb::SBDebugger::Initialize();

    // Create debugger instance
    debugger_ = lldb::SBDebugger::Create(false); // false = no source manager
    if (!debugger_.IsValid()) {
        setError("Failed to create LLDB debugger instance");
        return false;
    }

    // Don't echo commands
    debugger_.SetAsync(true);

    // Create a listener for process events
    listener_ = debugger_.GetListener();
    if (!listener_.IsValid()) {
        setError("Failed to create LLDB listener");
        lldb::SBDebugger::Destroy(debugger_);
        return false;
    }

    initialized_ = true;
    clearError();
    return true;
}

void LLDBController::shutdown() {
    if (!initialized_) {
        return;
    }

    // Detach if still attached
    if (isAttached()) {
        detach();
    }

    // Delete target if valid
    if (target_.IsValid()) {
        debugger_.DeleteTarget(target_);
    }

    // Destroy debugger
    if (debugger_.IsValid()) {
        lldb::SBDebugger::Destroy(debugger_);
    }

    // Terminate LLDB
    lldb::SBDebugger::Terminate();

    initialized_ = false;
}

// =============================================================================
// Process Attachment
// =============================================================================

bool LLDBController::attach(pid_t pid) {
    if (!initialized_) {
        setError("LLDB not initialized");
        return false;
    }

    // Create an empty target - LLDB will fill it in when we attach
    lldb::SBError error;
    target_ = debugger_.CreateTarget("", "", "", false, error);
    if (!target_.IsValid()) {
        setError("Failed to create target: " + std::string(error.GetCString() ? error.GetCString() : "unknown error"));
        return false;
    }

    // Attach to process
    lldb::SBAttachInfo attach_info(pid);
    attach_info.SetListener(listener_);

    process_ = target_.Attach(attach_info, error);
    if (!process_.IsValid() || error.Fail()) {
        setError("Failed to attach to PID " + std::to_string(pid) + ": " +
                 std::string(error.GetCString() ? error.GetCString() : "unknown error"));
        debugger_.DeleteTarget(target_);
        return false;
    }

    clearError();
    return true;
}

bool LLDBController::attachByName(const std::string &process_name, bool wait_for) {
    if (!initialized_) {
        setError("LLDB not initialized");
        return false;
    }

    // Create an empty target
    lldb::SBError error;
    target_ = debugger_.CreateTarget("", "", "", false, error);
    if (!target_.IsValid()) {
        setError("Failed to create target: " + std::string(error.GetCString() ? error.GetCString() : "unknown error"));
        return false;
    }

    // Attach by name
    lldb::SBAttachInfo attach_info;
    attach_info.SetExecutable(process_name.c_str());
    attach_info.SetWaitForLaunch(wait_for, false); // false = don't async
    attach_info.SetListener(listener_);

    process_ = target_.Attach(attach_info, error);
    if (!process_.IsValid() || error.Fail()) {
        setError("Failed to attach to process '" + process_name +
                 "': " + std::string(error.GetCString() ? error.GetCString() : "unknown error"));
        debugger_.DeleteTarget(target_);
        return false;
    }

    clearError();
    return true;
}

void LLDBController::detach() {
    if (!isAttached()) {
        return;
    }

    lldb::SBError error = process_.Detach();
    if (error.Fail()) {
        setError("Detach failed: " + std::string(error.GetCString() ? error.GetCString() : "unknown error"));
    } else {
        clearError();
    }
}

bool LLDBController::isAttached() const {
    if (!process_.IsValid()) {
        return false;
    }

    lldb::StateType state = process_.GetState();
    return state != lldb::eStateInvalid && state != lldb::eStateDetached && state != lldb::eStateExited;
}

pid_t LLDBController::targetPid() const {
    if (!process_.IsValid()) {
        return 0;
    }
    return static_cast<pid_t>(process_.GetProcessID());
}

std::string LLDBController::targetName() const {
    if (!target_.IsValid()) {
        return "";
    }

    lldb::SBFileSpec exe = target_.GetExecutable();
    if (!exe.IsValid()) {
        return "";
    }

    const char *filename = exe.GetFilename();
    return filename ? filename : "";
}

// =============================================================================
// Process Control
// =============================================================================

bool LLDBController::stop() {
    if (!isAttached()) {
        setError("Not attached to a process");
        return false;
    }

    lldb::SBError error = process_.Stop();
    if (error.Fail()) {
        setError("Failed to stop process: " + std::string(error.GetCString() ? error.GetCString() : "unknown error"));
        return false;
    }

    clearError();
    return true;
}

bool LLDBController::resume() {
    if (!isAttached()) {
        setError("Not attached to a process");
        return false;
    }

    lldb::SBError error = process_.Continue();
    if (error.Fail()) {
        setError("Failed to resume process: " + std::string(error.GetCString() ? error.GetCString() : "unknown error"));
        return false;
    }

    clearError();
    return true;
}

bool LLDBController::stepInto() {
    lldb::SBThread thread = getSelectedThreadInternal();
    if (!thread.IsValid()) {
        setError("No valid thread selected");
        return false;
    }

    thread.StepInto();
    clearError();
    return true;
}

bool LLDBController::stepOver() {
    lldb::SBThread thread = getSelectedThreadInternal();
    if (!thread.IsValid()) {
        setError("No valid thread selected");
        return false;
    }

    thread.StepOver();
    clearError();
    return true;
}

bool LLDBController::stepOut() {
    lldb::SBThread thread = getSelectedThreadInternal();
    if (!thread.IsValid()) {
        setError("No valid thread selected");
        return false;
    }

    thread.StepOut();
    clearError();
    return true;
}

ProcessState LLDBController::state() const {
    if (!process_.IsValid()) {
        return ProcessState::Invalid;
    }

    lldb::StateType lldb_state = process_.GetState();
    switch (lldb_state) {
    case lldb::eStateInvalid:
        return ProcessState::Invalid;
    case lldb::eStateRunning:
    case lldb::eStateStepping:
        return ProcessState::Running;
    case lldb::eStateStopped:
    case lldb::eStateSuspended:
        return ProcessState::Stopped;
    case lldb::eStateExited:
        return ProcessState::Exited;
    case lldb::eStateCrashed:
        return ProcessState::Crashed;
    case lldb::eStateDetached:
        return ProcessState::Detached;
    default:
        return ProcessState::Invalid;
    }
}

const std::string &LLDBController::lastError() const { return last_error_; }

// =============================================================================
// Thread Information
// =============================================================================

std::vector<ThreadInfo> LLDBController::getThreads() const {
    std::vector<ThreadInfo> result;

    if (!process_.IsValid()) {
        return result;
    }

    lldb::SBThread selected = process_.GetSelectedThread();
    uint64_t selected_id = selected.IsValid() ? selected.GetThreadID() : 0;

    uint32_t num_threads = process_.GetNumThreads();
    result.reserve(num_threads);

    for (uint32_t i = 0; i < num_threads; i++) {
        lldb::SBThread thread = process_.GetThreadAtIndex(i);
        if (thread.IsValid()) {
            bool is_selected = (thread.GetThreadID() == selected_id);
            result.push_back(threadToInfo(thread, is_selected));
        }
    }

    return result;
}

std::optional<ThreadInfo> LLDBController::getSelectedThread() const {
    lldb::SBThread thread = getSelectedThreadInternal();
    if (!thread.IsValid()) {
        return std::nullopt;
    }
    return threadToInfo(thread, true);
}

bool LLDBController::selectThread(uint64_t thread_id) {
    if (!process_.IsValid()) {
        setError("No valid process");
        return false;
    }

    uint32_t num_threads = process_.GetNumThreads();
    for (uint32_t i = 0; i < num_threads; i++) {
        lldb::SBThread thread = process_.GetThreadAtIndex(i);
        if (thread.IsValid() && thread.GetThreadID() == thread_id) {
            process_.SetSelectedThread(thread);
            clearError();
            return true;
        }
    }

    setError("Thread ID " + std::to_string(thread_id) + " not found");
    return false;
}

// =============================================================================
// Stack Frames
// =============================================================================

std::vector<FrameInfo> LLDBController::getFrames(uint32_t max_frames) const {
    std::vector<FrameInfo> result;

    lldb::SBThread thread = getSelectedThreadInternal();
    if (!thread.IsValid()) {
        return result;
    }

    uint32_t num_frames = thread.GetNumFrames();
    if (max_frames > 0 && num_frames > max_frames) {
        num_frames = max_frames;
    }

    result.reserve(num_frames);
    for (uint32_t i = 0; i < num_frames; i++) {
        lldb::SBFrame frame = thread.GetFrameAtIndex(i);
        if (frame.IsValid()) {
            result.push_back(frameToInfo(frame));
        }
    }

    return result;
}

std::optional<FrameInfo> LLDBController::getFrame(uint32_t frame_index) const {
    lldb::SBFrame frame = getFrameInternal(frame_index);
    if (!frame.IsValid()) {
        return std::nullopt;
    }
    return frameToInfo(frame);
}

// =============================================================================
// Variable Reading
// =============================================================================

std::optional<VariableInfo> LLDBController::readVariable(const std::string &name, uint32_t frame_index,
                                                          uint32_t expand_depth) const {
    lldb::SBFrame frame = getFrameInternal(frame_index);
    if (!frame.IsValid()) {
        return std::nullopt;
    }

    // Try to find the variable
    // First, check if it's a path expression (has dots or arrows)
    lldb::SBValue value;

    if (name.find('.') != std::string::npos || name.find("->") != std::string::npos || name.find('[') != std::string::npos) {
        // Path expression - use GetValueForVariablePath
        value = frame.GetValueForVariablePath(name.c_str());
    } else {
        // Simple name - try FindVariable first
        value = frame.FindVariable(name.c_str());

        // If not found, try as a register
        if (!value.IsValid()) {
            value = frame.FindRegister(name.c_str());
        }
    }

    if (!value.IsValid()) {
        return std::nullopt;
    }

    return valueToInfo(value, expand_depth);
}

std::vector<VariableInfo> LLDBController::listVariables(uint32_t frame_index, bool include_args, bool include_locals,
                                                         bool include_statics) const {
    std::vector<VariableInfo> result;

    lldb::SBFrame frame = getFrameInternal(frame_index);
    if (!frame.IsValid()) {
        return result;
    }

    lldb::SBValueList vars = frame.GetVariables(include_args, include_locals, include_statics,
                                                 true); // true = in_scope_only

    uint32_t num_vars = vars.GetSize();
    result.reserve(num_vars);

    for (uint32_t i = 0; i < num_vars; i++) {
        lldb::SBValue value = vars.GetValueAtIndex(i);
        if (value.IsValid()) {
            result.push_back(valueToInfo(value, 0)); // Don't expand by default
        }
    }

    return result;
}

// =============================================================================
// Breakpoints
// =============================================================================

int32_t LLDBController::setBreakpoint(const std::string &file, uint32_t line, const std::string &condition) {
    if (!target_.IsValid()) {
        setError("No valid target");
        return -1;
    }

    lldb::SBBreakpoint bp = target_.BreakpointCreateByLocation(file.c_str(), line);
    if (!bp.IsValid()) {
        setError("Failed to create breakpoint at " + file + ":" + std::to_string(line));
        return -1;
    }

    if (!condition.empty()) {
        bp.SetCondition(condition.c_str());
    }

    clearError();
    return static_cast<int32_t>(bp.GetID());
}

bool LLDBController::removeBreakpoint(int32_t breakpoint_id) {
    if (!target_.IsValid()) {
        setError("No valid target");
        return false;
    }

    bool result = target_.BreakpointDelete(static_cast<lldb::break_id_t>(breakpoint_id));
    if (!result) {
        setError("Failed to delete breakpoint " + std::to_string(breakpoint_id));
    } else {
        clearError();
    }
    return result;
}

std::vector<BreakpointInfo> LLDBController::getBreakpoints() const {
    std::vector<BreakpointInfo> result;

    if (!target_.IsValid()) {
        return result;
    }

    uint32_t num_breakpoints = target_.GetNumBreakpoints();
    result.reserve(num_breakpoints);

    for (uint32_t i = 0; i < num_breakpoints; i++) {
        lldb::SBBreakpoint bp = target_.GetBreakpointAtIndex(i);
        if (bp.IsValid()) {
            result.push_back(breakpointToInfo(bp));
        }
    }

    return result;
}

std::optional<BreakpointInfo> LLDBController::getBreakpoint(int32_t breakpoint_id) const {
    if (!target_.IsValid()) {
        return std::nullopt;
    }

    lldb::SBBreakpoint bp = target_.FindBreakpointByID(static_cast<lldb::break_id_t>(breakpoint_id));
    if (!bp.IsValid()) {
        return std::nullopt;
    }

    return breakpointToInfo(bp);
}

bool LLDBController::waitForBreakpoint(uint32_t timeout_ms) {
    if (!process_.IsValid()) {
        setError("No valid process");
        return false;
    }

    // First, resume the process if it's stopped
    if (state() == ProcessState::Stopped) {
        if (!resume()) {
            return false;
        }
    }

    // Wait for process to stop
    lldb::SBEvent event;
    uint32_t timeout_sec = (timeout_ms + 999) / 1000; // Round up to seconds

    while (true) {
        bool got_event = listener_.WaitForEvent(timeout_sec, event);
        if (!got_event) {
            setError("Timeout waiting for breakpoint");
            return false;
        }

        lldb::StateType new_state = lldb::SBProcess::GetStateFromEvent(event);
        if (new_state == lldb::eStateStopped) {
            clearError();
            return true;
        } else if (new_state == lldb::eStateExited || new_state == lldb::eStateCrashed ||
                   new_state == lldb::eStateDetached) {
            setError("Process exited/crashed while waiting for breakpoint");
            return false;
        }
    }
}

// =============================================================================
// Expression Evaluation
// =============================================================================

std::optional<VariableInfo> LLDBController::evaluateExpression(const std::string &expression,
                                                                uint32_t frame_index) const {
    lldb::SBFrame frame = getFrameInternal(frame_index);
    if (!frame.IsValid()) {
        return std::nullopt;
    }

    lldb::SBExpressionOptions options;
    options.SetIgnoreBreakpoints(true);
    options.SetFetchDynamicValue(lldb::eDynamicDontRunTarget);

    lldb::SBValue result = frame.EvaluateExpression(expression.c_str(), options);
    if (!result.IsValid()) {
        return std::nullopt;
    }

    lldb::SBError error = result.GetError();
    if (error.Fail()) {
        return std::nullopt;
    }

    return valueToInfo(result, 0);
}

// =============================================================================
// Private Helpers
// =============================================================================

void LLDBController::setError(const std::string &msg) const { last_error_ = msg; }

void LLDBController::clearError() const { last_error_.clear(); }

lldb::SBThread LLDBController::getSelectedThreadInternal() const {
    if (!process_.IsValid()) {
        return lldb::SBThread();
    }
    return process_.GetSelectedThread();
}

lldb::SBFrame LLDBController::getFrameInternal(uint32_t index) const {
    lldb::SBThread thread = getSelectedThreadInternal();
    if (!thread.IsValid()) {
        return lldb::SBFrame();
    }
    return thread.GetFrameAtIndex(index);
}

ThreadInfo LLDBController::threadToInfo(lldb::SBThread &thread, bool is_selected) {
    ThreadInfo info;
    info.id = thread.GetThreadID();
    info.index = thread.GetIndexID();
    info.is_selected = is_selected;

    const char *name = thread.GetName();
    info.name = name ? name : "";

    // Get stop reason
    lldb::StopReason stop_reason = thread.GetStopReason();
    switch (stop_reason) {
    case lldb::eStopReasonNone:
        info.stop_reason = "none";
        break;
    case lldb::eStopReasonBreakpoint:
        info.stop_reason = "breakpoint";
        break;
    case lldb::eStopReasonWatchpoint:
        info.stop_reason = "watchpoint";
        break;
    case lldb::eStopReasonSignal:
        info.stop_reason = "signal";
        break;
    case lldb::eStopReasonException:
        info.stop_reason = "exception";
        break;
    case lldb::eStopReasonPlanComplete:
        info.stop_reason = "step_complete";
        break;
    default:
        info.stop_reason = "other";
        break;
    }

    // Get current frame info
    lldb::SBFrame frame = thread.GetFrameAtIndex(0);
    if (frame.IsValid()) {
        const char *func_name = frame.GetFunctionName();
        info.function = func_name ? func_name : "";

        lldb::SBLineEntry line_entry = frame.GetLineEntry();
        if (line_entry.IsValid()) {
            lldb::SBFileSpec file_spec = line_entry.GetFileSpec();
            if (file_spec.IsValid()) {
                const char *filename = file_spec.GetFilename();
                info.file = filename ? filename : "";
            }
            info.line = line_entry.GetLine();
        }
    }

    return info;
}

FrameInfo LLDBController::frameToInfo(lldb::SBFrame &frame) {
    FrameInfo info;
    info.index = frame.GetFrameID();
    info.pc = frame.GetPC();
    info.fp = frame.GetFP();

    const char *func_name = frame.GetFunctionName();
    info.function = func_name ? func_name : "";

    lldb::SBLineEntry line_entry = frame.GetLineEntry();
    if (line_entry.IsValid()) {
        lldb::SBFileSpec file_spec = line_entry.GetFileSpec();
        if (file_spec.IsValid()) {
            const char *filename = file_spec.GetFilename();
            info.file = filename ? filename : "";
        }
        info.line = line_entry.GetLine();
    }

    return info;
}

VariableInfo LLDBController::valueToInfo(lldb::SBValue value, uint32_t expand_depth) const {
    VariableInfo info;
    info.is_valid = value.IsValid();

    if (!info.is_valid) {
        return info;
    }

    const char *name = value.GetName();
    info.name = name ? name : "";

    lldb::SBType type = value.GetType();
    if (type.IsValid()) {
        const char *type_name = type.GetName();
        info.type = type_name ? type_name : "";
        info.size = type.GetByteSize();
        info.is_pointer = type.IsPointerType();
        info.is_aggregate = type.IsAggregateType();
    }

    // Get value
    const char *val_str = value.GetValue();
    info.value = val_str ? val_str : "";

    // Get summary (for strings, etc.)
    const char *summary = value.GetSummary();
    info.summary = summary ? summary : "";

    // Get address
    info.address = value.GetLoadAddress();

    // Check for errors
    lldb::SBError error = value.GetError();
    if (error.Fail()) {
        info.is_valid = false;
    }

    // Expand children if requested
    if (expand_depth > 0 && info.is_aggregate) {
        uint32_t num_children = value.GetNumChildren();
        // Limit to reasonable number of children
        if (num_children > 100) {
            num_children = 100;
        }

        info.children.reserve(num_children);
        for (uint32_t i = 0; i < num_children; i++) {
            lldb::SBValue child = value.GetChildAtIndex(i);
            if (child.IsValid()) {
                info.children.push_back(valueToInfo(child, expand_depth - 1));
            }
        }
    }

    return info;
}

BreakpointInfo LLDBController::breakpointToInfo(lldb::SBBreakpoint bp) {
    BreakpointInfo info;
    info.id = static_cast<int32_t>(bp.GetID());
    info.enabled = bp.IsEnabled();
    info.hit_count = bp.GetHitCount();

    const char *condition = bp.GetCondition();
    info.condition = condition ? condition : "";

    // Get location info from first location
    info.resolved = (bp.GetNumResolvedLocations() > 0);

    if (bp.GetNumLocations() > 0) {
        lldb::SBBreakpointLocation loc = bp.GetLocationAtIndex(0);
        if (loc.IsValid()) {
            lldb::SBAddress addr = loc.GetAddress();
            if (addr.IsValid()) {
                lldb::SBLineEntry line_entry = addr.GetLineEntry();
                if (line_entry.IsValid()) {
                    lldb::SBFileSpec file_spec = line_entry.GetFileSpec();
                    if (file_spec.IsValid()) {
                        char path[1024];
                        file_spec.GetPath(path, sizeof(path));
                        info.file = path;
                    }
                    info.line = line_entry.GetLine();
                }
            }
        }
    }

    return info;
}

} // namespace ascii_query
