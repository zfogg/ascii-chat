"""Verify WebSocket dependency probes reject unavailable platform APIs."""
from pathlib import Path
import subprocess
import sys

import pytest

pytestmark = pytest.mark.unit


def test_websocket_dependency_probes(tmp_path, c_compiler):
    if sys.platform == "win32":
        pytest.skip("POSIX WebSocket dependency probe")
    root = Path(__file__).resolve().parents[3]
    fixture = tmp_path
    vendor = fixture / "vendor"
    (vendor / "lib/tls").mkdir(parents=True)
    for relative in ["lib/CMakeLists.txt", "lib/tls/CMakeLists.txt"]:
        (vendor / relative).write_text("", encoding="utf-8")
    (vendor / "empty.c").write_text("void websocket_probe_fixture(void) {}\n", encoding="utf-8")
    (vendor / "CMakeLists.txt").write_text("""
include(CheckFunctionExists)
check_function_exists(malloc LWS_HAVE_MALLOC)
check_function_exists(asciichat_missing_platform_api LWS_HAVE_MISSING)
if(NOT LWS_HAVE_MALLOC OR LWS_HAVE_MISSING)
    message(FATAL_ERROR "Platform function probes did not verify symbol availability")
endif()
add_library(websockets STATIC empty.c)
""", encoding="utf-8")
    (fixture / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.20)
project(websocket_probes C)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(ASCIICHAT_SHARED_DEPS OFF)
set(ASCIICHAT_BUILD_DEPS_FROM_SOURCE ON)
include("{root.as_posix()}/cmake/utils/SourceDependency.cmake")
macro(FetchContent_Declare)
endmacro()
macro(FetchContent_MakeAvailable)
    set(asciichat_libwebsockets_SOURCE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/vendor")
    set(asciichat_libwebsockets_BINARY_DIR "${{CMAKE_CURRENT_BINARY_DIR}}/vendor")
endmacro()
include("{root.as_posix()}/cmake/deps/Libwebsockets.cmake")
""", encoding="utf-8")
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(fixture), "-B", str(fixture / "build"),
                    f"-DCMAKE_C_COMPILER={c_compiler}"], check=True)
