/**
 * @file main.cpp
 * @brief ascii-query-server entry point
 *
 * This is the controller process for the query tool. It attaches to a target
 * process via LLDB and serves HTTP requests for variable inspection.
 *
 * Usage:
 *   ascii-query-server --attach <pid> --port 9999
 *   ascii-query-server --attach-name ascii-chat --port 9999
 *
 * @see docs/tooling/QUERY_TOOL_PLAN.md
 */

#include "http_server.h"
#include "json.h"
#include "lldb_controller.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

// Global controller and server (for signal handling)
ascii_query::LLDBController *g_controller = nullptr;
ascii_query::HttpServer *g_server = nullptr;
volatile sig_atomic_t g_shutdown_requested = 0;

void signalHandler(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
    if (g_server) {
        g_server->stop();
    }
}

void printUsage(const char *program) {
    fprintf(stderr, "Usage: %s [options]\n\n", program);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --attach <pid>         Attach to process by PID\n");
    fprintf(stderr, "  --attach-name <name>   Attach to process by name\n");
    fprintf(stderr, "  --wait                 Wait for process to start (with --attach-name)\n");
    fprintf(stderr, "  --port <port>          HTTP server port (default: 9999)\n");
    fprintf(stderr, "  --help                 Show this help\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s --attach 12345 --port 9999\n", program);
    fprintf(stderr, "  %s --attach-name ascii-chat --wait\n", program);
    fprintf(stderr, "\nQuery endpoints:\n");
    fprintf(stderr, "  GET  /                 Status page\n");
    fprintf(stderr, "  GET  /process          Process information\n");
    fprintf(stderr, "  GET  /threads          Thread list\n");
    fprintf(stderr, "  GET  /frames           Stack frames (when stopped)\n");
    fprintf(stderr, "  GET  /query            Query a variable\n");
    fprintf(stderr, "  POST /continue         Resume execution\n");
    fprintf(stderr, "  POST /step             Single step\n");
    fprintf(stderr, "  POST /detach           Detach from process\n");
}

// Convert ProcessState to string
const char *processStateToString(ascii_query::ProcessState state) {
    switch (state) {
    case ascii_query::ProcessState::Invalid:
        return "invalid";
    case ascii_query::ProcessState::Running:
        return "running";
    case ascii_query::ProcessState::Stopped:
        return "stopped";
    case ascii_query::ProcessState::Exited:
        return "exited";
    case ascii_query::ProcessState::Crashed:
        return "crashed";
    case ascii_query::ProcessState::Detached:
        return "detached";
    }
    return "unknown";
}

// Build JSON for ThreadInfo
ascii_query::json::JsonObject threadToJson(const ascii_query::ThreadInfo &thread) {
    ascii_query::json::JsonObject obj;
    obj.set("id", thread.id);
    obj.set("index", static_cast<int64_t>(thread.index));
    obj.set("name", thread.name);
    obj.set("stop_reason", thread.stop_reason);
    obj.set("function", thread.function);
    obj.set("file", thread.file);
    obj.set("line", static_cast<int64_t>(thread.line));
    obj.set("selected", thread.is_selected);
    return obj;
}

// Build JSON for FrameInfo
ascii_query::json::JsonObject frameToJson(const ascii_query::FrameInfo &frame) {
    ascii_query::json::JsonObject obj;
    obj.set("index", static_cast<int64_t>(frame.index));
    obj.set("function", frame.function);
    obj.set("file", frame.file);
    obj.set("line", static_cast<int64_t>(frame.line));
    obj.set("pc", frame.pc);
    obj.set("fp", frame.fp);
    return obj;
}

// Build JSON for VariableInfo
ascii_query::json::JsonObject variableToJson(const ascii_query::VariableInfo &var, int max_depth = 3) {
    ascii_query::json::JsonObject obj;
    obj.set("name", var.name);
    obj.set("type", var.type);
    obj.set("value", var.value);
    if (!var.summary.empty()) {
        obj.set("summary", var.summary);
    }
    obj.set("address", var.address);
    obj.set("size", static_cast<int64_t>(var.size));
    obj.set("valid", var.is_valid);
    obj.set("pointer", var.is_pointer);
    obj.set("aggregate", var.is_aggregate);

    if (!var.children.empty() && max_depth > 0) {
        ascii_query::json::JsonArray children;
        for (const auto &child : var.children) {
            children.add(variableToJson(child, max_depth - 1));
        }
        obj.set("children", children);
    }

    return obj;
}

// Build JSON for BreakpointInfo
ascii_query::json::JsonObject breakpointToJson(const ascii_query::BreakpointInfo &bp) {
    ascii_query::json::JsonObject obj;
    obj.set("id", static_cast<int64_t>(bp.id));
    obj.set("file", bp.file);
    obj.set("line", static_cast<int64_t>(bp.line));
    obj.set("condition", bp.condition);
    obj.set("hit_count", static_cast<int64_t>(bp.hit_count));
    obj.set("enabled", bp.enabled);
    obj.set("resolved", bp.resolved);
    return obj;
}

// Setup HTTP routes
void setupRoutes(ascii_query::HttpServer &server, ascii_query::LLDBController &controller) {
    using namespace ascii_query;
    using namespace ascii_query::json;

    // GET / - Status page (HTML)
    server.addRoute("GET", "/", [&controller](const HttpRequest &) {
        ProcessState state = controller.state();
        std::string state_str = processStateToString(state);

        std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <title>ascii-query-server</title>
    <style>
        body { font-family: system-ui, sans-serif; max-width: 800px; margin: 40px auto; padding: 0 20px; }
        h1 { color: #333; }
        .status { display: inline-block; padding: 4px 12px; border-radius: 4px; font-weight: bold; }
        .status.running { background: #d4edda; color: #155724; }
        .status.stopped { background: #fff3cd; color: #856404; }
        .status.exited, .status.crashed { background: #f8d7da; color: #721c24; }
        .status.detached, .status.invalid { background: #e2e3e5; color: #383d41; }
        code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }
        pre { background: #f4f4f4; padding: 12px; border-radius: 6px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>ascii-query-server</h1>
    <p>
        <strong>Target PID:</strong> )" +
                           std::to_string(controller.targetPid()) + R"(<br>
        <strong>Target Name:</strong> )" +
                           controller.targetName() + R"(<br>
        <strong>State:</strong> <span class="status )" +
                           state_str + R"(">)" + state_str + R"(</span>
    </p>
    <h2>Endpoints</h2>
    <ul>
        <li><code>GET /process</code> - Process information</li>
        <li><code>GET /threads</code> - Thread list</li>
        <li><code>GET /frames</code> - Stack frames (when stopped)</li>
        <li><code>GET /query?file=X&amp;line=N&amp;name=VAR</code> - Query variable</li>
        <li><code>GET /breakpoints</code> - List breakpoints</li>
        <li><code>POST /continue</code> - Resume execution</li>
        <li><code>POST /stop</code> - Stop execution</li>
        <li><code>POST /step</code> - Single step</li>
    </ul>
    <h2>Example</h2>
    <pre>curl 'localhost:9999/query?file=src/server.c&amp;line=100&amp;name=client_count&amp;break'
curl 'localhost:9999/query?name=frame.width'
curl -X POST 'localhost:9999/continue'</pre>
</body>
</html>
)";
        return HttpResponse::html(html);
    });

    // GET /process - Process information
    server.addRoute("GET", "/process", [&controller](const HttpRequest &) {
        JsonObject obj;
        obj.set("pid", static_cast<int64_t>(controller.targetPid()));
        obj.set("name", controller.targetName());
        obj.set("state", processStateToString(controller.state()));
        obj.set("attached", controller.isAttached());
        if (!controller.lastError().empty()) {
            obj.set("last_error", controller.lastError());
        }
        return HttpResponse::json(obj.toString());
    });

    // GET /threads - Thread list
    server.addRoute("GET", "/threads", [&controller](const HttpRequest &) {
        auto threads = controller.getThreads();
        JsonArray arr;
        for (const auto &thread : threads) {
            arr.add(threadToJson(thread));
        }
        JsonObject obj;
        obj.set("count", static_cast<int64_t>(threads.size()));
        obj.set("threads", arr);
        return HttpResponse::json(obj.toString());
    });

    // GET /frames - Stack frames
    server.addRoute("GET", "/frames", [&controller](const HttpRequest &req) {
        int max_frames = req.paramInt("max", 50);
        auto frames = controller.getFrames(static_cast<uint32_t>(max_frames));
        JsonArray arr;
        for (const auto &frame : frames) {
            arr.add(frameToJson(frame));
        }
        JsonObject obj;
        obj.set("count", static_cast<int64_t>(frames.size()));
        obj.set("frames", arr);
        return HttpResponse::json(obj.toString());
    });

    // GET /query - Query a variable
    server.addRoute("GET", "/query", [&controller](const HttpRequest &req) {
        std::string file = req.param("file");
        int line = req.paramInt("line", 0);
        std::string name = req.param("name");
        int frame_index = req.paramInt("frame", 0);
        int expand_depth = req.paramInt("depth", 0);
        bool should_break = req.hasParam("break");
        int timeout_ms = req.paramInt("timeout", 5000);

        if (name.empty()) {
            return HttpResponse::badRequest("Missing 'name' parameter");
        }

        ProcessState state = controller.state();
        bool is_stopped = (state == ProcessState::Stopped);

        // If we have file:line and need to break, set breakpoint
        if (!file.empty() && line > 0 && should_break && !is_stopped) {
            int bp_id = controller.setBreakpoint(file, static_cast<uint32_t>(line));
            if (bp_id < 0) {
                JsonObject obj;
                obj.set("status", "error");
                obj.set("error", "breakpoint_failed");
                obj.set("message", "Failed to set breakpoint at " + file + ":" + std::to_string(line));
                return HttpResponse::json(obj.toString());
            }

            bool hit = controller.waitForBreakpoint(static_cast<uint32_t>(timeout_ms));
            if (!hit) {
                controller.removeBreakpoint(bp_id);
                JsonObject obj;
                obj.set("status", "error");
                obj.set("error", "timeout");
                obj.set("message", "Breakpoint not hit within " + std::to_string(timeout_ms) + "ms");
                return HttpResponse::json(obj.toString());
            }
            is_stopped = true;
        }

        // Read the variable
        int actual_expand = expand_depth > 0 ? expand_depth : (req.hasParam("expand") ? 3 : 0);
        auto var = controller.readVariable(name, static_cast<uint32_t>(frame_index),
                                           static_cast<uint32_t>(actual_expand));

        if (!var) {
            JsonObject obj;
            obj.set("status", "error");
            obj.set("error", "not_found");
            obj.set("message", "Variable '" + name + "' not found");
            obj.set("stopped", is_stopped);
            return HttpResponse::json(obj.toString());
        }

        JsonObject obj;
        obj.set("status", "ok");
        obj.set("stopped", is_stopped);
        obj.set("result", variableToJson(*var, actual_expand > 0 ? actual_expand : 3));
        return HttpResponse::json(obj.toString());
    });

    // GET /breakpoints - List breakpoints
    server.addRoute("GET", "/breakpoints", [&controller](const HttpRequest &) {
        auto breakpoints = controller.getBreakpoints();
        JsonArray arr;
        for (const auto &bp : breakpoints) {
            arr.add(breakpointToJson(bp));
        }
        JsonObject obj;
        obj.set("count", static_cast<int64_t>(breakpoints.size()));
        obj.set("breakpoints", arr);
        return HttpResponse::json(obj.toString());
    });

    // POST /breakpoints - Set breakpoint
    server.addRoute("POST", "/breakpoints", [&controller](const HttpRequest &req) {
        std::string file = req.param("file");
        int line = req.paramInt("line", 0);
        std::string condition = req.param("condition");

        if (file.empty() || line <= 0) {
            return HttpResponse::badRequest("Missing 'file' and 'line' parameters");
        }

        int bp_id = controller.setBreakpoint(file, static_cast<uint32_t>(line), condition);
        if (bp_id < 0) {
            JsonObject obj;
            obj.set("status", "error");
            obj.set("message", controller.lastError());
            return HttpResponse::json(obj.toString());
        }

        auto bp = controller.getBreakpoint(bp_id);
        JsonObject obj;
        obj.set("status", "ok");
        if (bp) {
            obj.set("breakpoint", breakpointToJson(*bp));
        }
        return HttpResponse::json(obj.toString());
    });

    // DELETE /breakpoints - Remove breakpoint
    server.addRoute("DELETE", "/breakpoints", [&controller](const HttpRequest &req) {
        int bp_id = req.paramInt("id", -1);
        if (bp_id < 0) {
            return HttpResponse::badRequest("Missing 'id' parameter");
        }
        bool removed = controller.removeBreakpoint(static_cast<int32_t>(bp_id));
        JsonObject obj;
        obj.set("status", removed ? "ok" : "error");
        if (!removed) {
            obj.set("message", "Breakpoint not found");
        }
        return HttpResponse::json(obj.toString());
    });

    // POST /continue - Resume execution
    server.addRoute("POST", "/continue", [&controller](const HttpRequest &) {
        bool resumed = controller.resume();
        JsonObject obj;
        obj.set("status", resumed ? "running" : "error");
        if (!resumed) {
            obj.set("error", controller.lastError());
        }
        return HttpResponse::json(obj.toString());
    });

    // POST /stop - Stop execution
    server.addRoute("POST", "/stop", [&controller](const HttpRequest &) {
        bool stopped = controller.stop();
        JsonObject obj;
        obj.set("status", stopped ? "stopped" : "error");
        if (!stopped) {
            obj.set("error", controller.lastError());
        }
        return HttpResponse::json(obj.toString());
    });

    // POST /step - Single step
    server.addRoute("POST", "/step", [&controller](const HttpRequest &req) {
        bool over = req.hasParam("over");
        bool out = req.hasParam("out");
        bool success = out ? controller.stepOut() : (over ? controller.stepOver() : controller.stepInto());
        JsonObject obj;
        obj.set("status", success ? "ok" : "error");
        if (!success) {
            obj.set("error", controller.lastError());
        }
        return HttpResponse::json(obj.toString());
    });

    // POST /detach - Detach from process
    server.addRoute("POST", "/detach", [&controller](const HttpRequest &) {
        controller.detach();
        JsonObject obj;
        obj.set("status", "detached");
        return HttpResponse::json(obj.toString());
    });
}

} // namespace

int main(int argc, char *argv[]) {
    // Parse command line arguments
    pid_t attach_pid = 0;
    std::string attach_name;
    bool wait_for = false;
    uint16_t port = 9999;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--attach") == 0 && i + 1 < argc) {
            attach_pid = static_cast<pid_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--attach-name") == 0 && i + 1 < argc) {
            attach_name = argv[++i];
        } else if (strcmp(argv[i], "--wait") == 0) {
            wait_for = true;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(atoi(argv[++i]));
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Validate arguments
    if (attach_pid == 0 && attach_name.empty()) {
        fprintf(stderr, "Error: Must specify --attach <pid> or --attach-name <name>\n\n");
        printUsage(argv[0]);
        return 1;
    }

    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    // Initialize LLDB controller
    ascii_query::LLDBController controller;
    g_controller = &controller;

    if (!controller.initialize()) {
        fprintf(stderr, "Error: Failed to initialize LLDB: %s\n", controller.lastError().c_str());
        return 1;
    }

    // Attach to target
    fprintf(stderr, "[ascii-query-server] Attaching to ");
    if (attach_pid > 0) {
        fprintf(stderr, "PID %d...\n", attach_pid);
        if (!controller.attach(attach_pid)) {
            fprintf(stderr, "Error: Failed to attach: %s\n", controller.lastError().c_str());
            return 1;
        }
    } else {
        fprintf(stderr, "process '%s'%s...\n", attach_name.c_str(), wait_for ? " (waiting)" : "");
        if (!controller.attachByName(attach_name, wait_for)) {
            fprintf(stderr, "Error: Failed to attach: %s\n", controller.lastError().c_str());
            return 1;
        }
    }

    fprintf(stderr, "[ascii-query-server] Attached to %s (PID %d)\n", controller.targetName().c_str(),
            controller.targetPid());

    // Resume the target (it's stopped after attach)
    if (controller.state() == ascii_query::ProcessState::Stopped) {
        fprintf(stderr, "[ascii-query-server] Resuming target...\n");
        controller.resume();
    }

    // Setup HTTP server
    ascii_query::HttpServer server;
    g_server = &server;

    setupRoutes(server, controller);

    // Start HTTP server
    if (!server.start(port)) {
        fprintf(stderr, "Error: Failed to start HTTP server: %s\n", server.lastError().c_str());
        controller.detach();
        return 1;
    }

    fprintf(stderr, "[ascii-query-server] HTTP server listening on http://localhost:%d\n", port);
    fprintf(stderr, "[ascii-query-server] Press Ctrl+C to stop\n");

    // Wait for shutdown
    while (!g_shutdown_requested && controller.isAttached()) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
        ascii_query::ProcessState state = controller.state();
        if (state == ascii_query::ProcessState::Exited || state == ascii_query::ProcessState::Crashed) {
            fprintf(stderr, "[ascii-query-server] Target %s, shutting down\n",
                    state == ascii_query::ProcessState::Exited ? "exited" : "crashed");
            break;
        }
    }

    // Cleanup
    fprintf(stderr, "[ascii-query-server] Shutting down...\n");
    server.stop();
    controller.detach();
    controller.shutdown();

    fprintf(stderr, "[ascii-query-server] Done\n");
    return 0;
}
