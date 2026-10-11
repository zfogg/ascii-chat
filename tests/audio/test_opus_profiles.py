"""Check codec selection for client/discovery and actual media availability."""

import ctypes as c
from pathlib import Path
import subprocess
import sys
from pipeline_ctypes import Config
from test_spectral import load

library = Path(sys.argv[1]).resolve()
if len(sys.argv) == 2:
    for mode in ("client", "discovery"):
        for source in ("auto", "mic", "media", "both", "remote"):
            for test in ("false", "true"):
                subprocess.run(
                    [sys.executable, __file__, str(library), mode, source, test],
                    check=True,
                )
    print(
        "PASS: client/discovery, all capture selections, video-only/media audio, and test audio"
    )
    raise SystemExit(0)
mode, source, test = sys.argv[2:]
lib = load(library)
lib.asciichat_shared_init.argtypes = [c.c_char_p, c.c_bool, c.c_bool]
lib.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
lib.client_audio_pipeline_config_for_source.argtypes = [c.c_bool]
lib.client_audio_pipeline_config_for_source.restype = Config
assert lib.asciichat_shared_init(None, False, False) == 0
args = ["ascii-chat", "--log-level=error"]
args.append("blue-mountain-tiger" if mode == "discovery" else mode)
args += [f"--audio-capture-source={source}", f"--test-audio={test}"]
encoded = [arg.encode() for arg in args]
argv = (c.c_char_p * (len(encoded) + 1))(*encoded, None)
assert lib.options_init(len(encoded), argv) == 0
for has_audio in (False, True):
    config = lib.client_audio_pipeline_config_for_source(has_audio)
    media = test == "true" or (has_audio and source in ("auto", "media", "both"))
    assert (config.opus_application, config.opus_bitrate) == (
        (2049, 128000) if media else (2048, 32000)
    ), (mode, source, test, has_audio)
lib.options_state_destroy()
lib.asciichat_shared_destroy()
