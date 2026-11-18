# Common dependency target contract.
include_guard(GLOBAL)

function(asciichat_dependency_interface name)
    set(_target "asciichat-${name}")
    if(NOT TARGET "${_target}")
        add_library("${_target}" INTERFACE)
    endif()
    set(ASCIICHAT_DEPENDENCY_TARGET_${name} "${_target}" CACHE INTERNAL
        "ascii-chat target for ${name}" FORCE)
endfunction()

function(asciichat_dependency_imported name type location include_dir)
    set(_target "asciichat-${name}")
    if(NOT TARGET "${_target}")
        add_library("${_target}" "${type}" IMPORTED GLOBAL)
    endif()
    set_target_properties("${_target}" PROPERTIES
        IMPORTED_LOCATION "${location}"
        INTERFACE_INCLUDE_DIRECTORIES "${include_dir}")
    set(ASCIICHAT_DEPENDENCY_TARGET_${name} "${_target}" CACHE INTERNAL
        "ascii-chat target for ${name}" FORCE)
endfunction()

function(asciichat_dependency_link target dependency)
    if(TARGET "asciichat-${dependency}")
        target_link_libraries("${target}" INTERFACE "asciichat-${dependency}")
    else()
        message(FATAL_ERROR "Dependency target asciichat-${dependency} was not configured")
    endif()
endfunction()
