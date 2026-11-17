# A Clang musl toolchain for packaged musl SDKs and optional complete sysroots.
include_guard(GLOBAL)
set(CMAKE_SYSTEM_NAME Linux)
if(NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if(NOT CMAKE_SYSTEM_PROCESSOR)
        execute_process(COMMAND uname -m OUTPUT_VARIABLE CMAKE_SYSTEM_PROCESSOR
            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    endif()
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(MUSL_ARCH aarch64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(MUSL_ARCH x86_64)
else()
    message(FATAL_ERROR "Unsupported musl architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(MUSL_TARGET "${MUSL_ARCH}-linux-musl" CACHE STRING "musl target")
if(CMAKE_C_COMPILER MATCHES "(^|/)(gcc|g(cc)?-[0-9]+)$")
    set(_asciichat_musl_gcc "${CMAKE_CURRENT_LIST_DIR}/musl-gcc-wrapper.sh")
    set(_asciichat_gxx "${CMAKE_CURRENT_LIST_DIR}/musl-g++-wrapper.sh")
    set(CMAKE_C_COMPILER "${_asciichat_musl_gcc}" CACHE FILEPATH "GCC musl C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_asciichat_gxx}" CACHE FILEPATH "GCC C++ compiler" FORCE)
    set(MUSL_GCC_RUNTIME_DIR
        "${ASCIICHAT_DEPS_CACHE_DIR}/alpine-gcc/usr/lib"
        CACHE PATH "Cached Alpine GCC runtime libraries" FORCE)
    # The Alpine GCC package provides the compiler/runtime, while the C++
    # headers and static libstdc++ are staged separately in the dependency
    # cache. Make those paths part of the toolchain flags before any
    # dependency project is configured.
    file(GLOB _asciichat_libstdcpp_roots LIST_DIRECTORIES true
        "${ASCIICHAT_DEPS_CACHE_DIR}/alpine-libstdcxx/usr/include/c++/*")
    list(SORT _asciichat_libstdcpp_roots)
    list(GET _asciichat_libstdcpp_roots 0 MUSL_LIBSTDCPP_INCLUDE_DIR)
    if(MUSL_LIBSTDCPP_INCLUDE_DIR)
        file(GLOB _asciichat_libstdcpp_target_roots LIST_DIRECTORIES true
            "${MUSL_LIBSTDCPP_INCLUDE_DIR}/*-linux-musl")
        list(SORT _asciichat_libstdcpp_target_roots)
        list(GET _asciichat_libstdcpp_target_roots 0 MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR)
    endif()
    set(_asciichat_ssp_nonshared "${MUSL_GCC_RUNTIME_DIR}/libssp_nonshared.a")
    if(NOT EXISTS "${_asciichat_ssp_nonshared}")
        find_program(_asciichat_ar NAMES ar llvm-ar REQUIRED)
        file(MAKE_DIRECTORY "${MUSL_GCC_RUNTIME_DIR}")
        execute_process(COMMAND "${_asciichat_ar}" rcs "${_asciichat_ssp_nonshared}"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
else()
    find_program(CMAKE_C_COMPILER NAMES clang REQUIRED)
    find_program(CMAKE_CXX_COMPILER NAMES clang++ REQUIRED)
    set(CMAKE_C_COMPILER_TARGET "${MUSL_TARGET}")
    set(CMAKE_CXX_COMPILER_TARGET "${MUSL_TARGET}")
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(ASCIICHAT_MUSL_SYSROOT)
    set(CMAKE_SYSROOT "${ASCIICHAT_MUSL_SYSROOT}")
    set(_musl_include_paths "${CMAKE_SYSROOT}/usr/include" "${CMAKE_SYSROOT}/include")
    set(_musl_lib_paths "${CMAKE_SYSROOT}/usr/lib" "${CMAKE_SYSROOT}/lib")
else()
    set(_musl_include_paths /usr/lib/musl/include /usr/include/${MUSL_TARGET}
        /usr/${MUSL_TARGET}/include /usr/lib/${MUSL_TARGET}/include)
    set(_musl_lib_paths /usr/lib/musl/lib /usr/lib/${MUSL_TARGET} /usr/${MUSL_TARGET}/lib64)
endif()
find_path(MUSL_INCLUDE_DIR stdlib.h PATHS ${_musl_include_paths} NO_DEFAULT_PATH REQUIRED)
find_path(MUSL_LIBDIR libc.a PATHS ${_musl_lib_paths} NO_DEFAULT_PATH REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/MuslFlags.cmake")
asciichat_musl_compile_flags(_musl_c_flags _musl_cxx_flags)
string(APPEND CMAKE_C_FLAGS_INIT " ${_musl_c_flags}")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${_musl_cxx_flags}")

if(CMAKE_C_COMPILER MATCHES "musl-gcc|(^|/)(gcc|g(cc)?-[0-9]+)$")
    set(_musl_exe_link "-static-pie -nostdlib -L${MUSL_GCC_RUNTIME_DIR} -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/rcrt1.o ${MUSL_LIBDIR}/crti.o")
    set(_musl_shared_link "-nostdlib -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/crti.o")
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-libgcc-file-name
        OUTPUT_VARIABLE _musl_libgcc OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(NOT EXISTS "${_musl_libgcc}")
        message(FATAL_ERROR "GCC musl runtime archive was not found: ${_musl_libgcc}")
    endif()
else()
    set(_musl_exe_link "-fuse-ld=lld -static-pie -nostdlib -nostdlib++ -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/rcrt1.o ${MUSL_LIBDIR}/crti.o")
    set(_musl_shared_link "-fuse-ld=lld -nostdlib -nostdlib++ -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/crti.o")
endif()
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${_musl_exe_link}")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${_musl_shared_link}")
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " ${_musl_shared_link}")
if(_musl_libgcc)
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "${_musl_libgcc} -lc -lm ${MUSL_LIBDIR}/crtn.o")
else()
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "-lc -lm ${MUSL_LIBDIR}/crtn.o")
endif()
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${CMAKE_C_STANDARD_LIBRARIES_INIT}")

# Programs execute on the host; headers, libraries and packages belong to the target.
list(APPEND CMAKE_FIND_ROOT_PATH "${MUSL_LIBDIR}" "${MUSL_INCLUDE_DIR}"
    "${ASCIICHAT_DEPS_CACHE_DIR}" "${ASCIICHAT_DEPS_CACHE_DIR}/installed")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MUSL_TARGET MUSL_INCLUDE_DIR
    MUSL_LIBDIR ASCIICHAT_MUSL_SYSROOT ASCIICHAT_DEPS_CACHE_DIR KERNEL_HEADERS_DIR)
