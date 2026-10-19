include_guard(GLOBAL)

function(asciichat_wasm_make_commands build_command install_command)
    if(CMAKE_HOST_WIN32)
        # Git Bash puts its Unix tools first so they can read configure's /c/... paths.
        set(${build_command}
            "${ASCIICHAT_BASH_EXECUTABLE}" -lc "make -j SHELL=C:/Progra~1/Git/usr/bin/sh.exe"
            PARENT_SCOPE
        )
        set(${install_command}
            "${ASCIICHAT_BASH_EXECUTABLE}" -lc "make SHELL=C:/Progra~1/Git/usr/bin/sh.exe install"
            PARENT_SCOPE
        )
    else()
        set(${build_command} make -j PARENT_SCOPE)
        set(${install_command} make install PARENT_SCOPE)
    endif()
endfunction()
