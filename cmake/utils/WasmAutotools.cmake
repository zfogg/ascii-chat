include_guard(GLOBAL)

function(asciichat_wasm_make_commands build_command install_command)
    if(CMAKE_HOST_WIN32)
        # Git Bash puts its Unix tools first so they can read configure's /c/... paths.
        # Keep SHELL indirect: Windows Make otherwise expands sh.exe to an unquoted
        # absolute path, breaking Autotools recipes when Git's path contains spaces.
        set(_shell "CONFIG_SHELL=sh.exe 'SHELL=$(CONFIG_SHELL)'")
        set(${build_command}
            "${ASCIICHAT_BASH_EXECUTABLE}" -lc "make -j ${_shell}"
            PARENT_SCOPE
        )
        set(${install_command}
            "${ASCIICHAT_BASH_EXECUTABLE}" -lc "make ${_shell} install"
            PARENT_SCOPE
        )
    else()
        set(${build_command} make -j PARENT_SCOPE)
        set(${install_command} make install PARENT_SCOPE)
    endif()
endfunction()
