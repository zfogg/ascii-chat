# Criterion can report a passing suite even when a worker reports a leak.
function(asciichat_fail_on_lsan_report test_name)
    set_property(TEST ${test_name} APPEND PROPERTY FAIL_REGULAR_EXPRESSION
        "LeakSanitizer: detected memory leaks" "LeakSanitizer has encountered a fatal error")
endfunction()
