#!/usr/bin/env python3
"""Check option metadata and boolean parsing against a built shared library.

Usage: python tests/integration/options_metadata.py build/bin/asciichat.dll
The shared library must export the public options API.
"""
import ctypes as c
import os
from pathlib import Path
import sys

library = Path(sys.argv[1]).resolve()
if os.name == "nt":
    dll_directory = os.add_dll_directory(str(library.parent))
lib = c.CDLL(str(library))


def function(name, result, *arguments):
    fn = getattr(lib, name)
    fn.restype = result
    fn.argtypes = list(arguments)
    return fn


values = function("options_registry_get_enum_values", c.POINTER(c.c_char_p),
                  c.c_char_p, c.c_void_p, c.POINTER(c.c_size_t))
suggest = function("asciichat_suggest_enum_value", c.c_char_p, c.c_char_p, c.c_char_p)
is_enum = function("options_is_enum_option", c.c_bool, c.c_char_p)
checks = {
    "color-mode": ("treucolor", "truecolor"),
    "render-mode": ("foregroud", "foreground"),
    "render-theme": ("drak", "dark"),
    "audio-source": ("medai", "media"),
    "audio-capture-source": ("remtoe", "remote"),
    "palette": ("standrad", "standard"),
    "color-filter": ("rainobw", "rainbow"),
    "log-level": ("debg", "debug"),
    "color": ("ture", "true"),
    "utf8": ("flase", "false"),
    "completions": ("powreshell", "powershell"),
}
for option, (typo, expected) in checks.items():
    count = c.c_size_t()
    choices = values(option.encode(), None, c.byref(count))
    assert choices and count.value
    assert choices[count.value] is None
    assert expected.encode() in [choices[i] for i in range(count.value)]
    assert is_enum(option.encode())
    assert suggest(option.encode(), typo.encode()) == expected.encode(), option
    print(f"PASS registry and typo suggestion: {option}")

create = function("options_builder_create", c.c_void_p, c.c_size_t)
add_bool = function("options_builder_add_bool", None, c.c_void_p, c.c_char_p, c.c_char,
                    c.c_size_t, c.c_bool, c.c_char_p, c.c_char_p, c.c_bool, c.c_char_p)
build = function("options_builder_build", c.c_void_p, c.c_void_p)
destroy_builder = function("options_builder_destroy", None, c.c_void_p)
destroy_config = function("options_config_destroy", None, c.c_void_p)
parse = function("options_config_parse", c.c_int, c.c_void_p, c.c_int,
                 c.POINTER(c.c_char_p), c.c_void_p, c.c_int, c.POINTER(c.c_int),
                 c.POINTER(c.POINTER(c.c_char_p)))

builder = create(c.sizeof(c.c_bool))
assert builder
add_bool(builder, b"toggle", b't', 0, False, b"Test toggle", b"TEST", False, None)
config = build(builder)
assert config
destroy_builder(builder)
try:
    for arguments, expected, remaining in [
        ([b"--toggle"], True, []),
        ([b"--toggle", b"true"], True, []),
        ([b"--toggle", b"false"], False, []),
        ([b"--toggle=false"], False, []),
        ([b"--toggle", b"session-name"], True, [b"session-name"]),
    ]:
        argv = (c.c_char_p * (len(arguments) + 1))(b"ascii-chat", *arguments)
        state = c.c_bool(False)
        argc_left = c.c_int()
        argv_left = c.POINTER(c.c_char_p)()
        result = parse(config, len(argv), argv, c.byref(state), 0,
                       c.byref(argc_left), c.byref(argv_left))
        if remaining:
            # This config has no positional schema: a non-boolean must remain
            # positional and be rejected, rather than silently consumed.
            assert result != 0 and state.value is True, arguments
            print(f"PASS non-boolean remains positional: {arguments}")
            continue
        assert result == 0, (arguments, result)
        assert state.value == expected, arguments
        assert [argv_left[i] for i in range(argc_left.value)] == remaining, arguments
        print(f"PASS boolean parsing: {arguments}")
finally:
    destroy_config(config)

callback = function("options_action_callback", c.c_void_p, c.c_char_p, c.c_bool)
for name, normal, early in [
    ("list-webcams", "action_list_webcams", "action_list_webcams"),
    ("list-microphones", "action_list_microphones", "action_list_microphones"),
    ("list-speakers", "action_list_speakers", "action_list_speakers"),
    ("show-capabilities", "action_show_capabilities", "action_show_capabilities_immediate"),
    ("check-update", "action_check_update", "action_check_update_immediate"),
]:
    assert callback(name.encode(), False) == c.cast(getattr(lib, normal), c.c_void_p).value
    assert callback(name.encode(), True) == c.cast(getattr(lib, early), c.c_void_p).value
    print(f"PASS action dispatch: {name}")
assert callback(b"help", True) is None
assert callback(None, False) is None
print("All metadata, boolean parsing, and action dispatch checks passed.")
