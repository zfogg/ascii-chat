"""Check codec selection for client/discovery and actual media availability."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import ctypes as c
from pathlib import Path
import subprocess
import sys
from ascii_chat_tests.pipeline_ctypes import Config
from ascii_chat_tests.audio_spectral import load

library = Path(sys.argv[1]).resolve()
if len(sys.argv) == 2:
    for mode in ("client", "discovery"):
        for source in ("auto", "mic", "media", "both", "remote"):
            for test in ("false", "true"):
                for channels in ("1", "2"):
                    subprocess.run(
                        [sys.executable, __file__, str(library), mode, source, test, channels],
                        check=True,
                    )
    print(
        "PASS: client/discovery, all capture selections, mono/stereo microphones, media and test audio"
    )
    raise SystemExit(0)
mode, source, test, channels = sys.argv[2:]
lib = load(library)
lib.asciichat_shared_init.argtypes = [c.c_char_p, c.c_bool, c.c_bool]
lib.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
lib.client_audio_pipeline_config_for_source.argtypes = [c.c_bool]
lib.client_audio_pipeline_config_for_source.restype = Config
assert lib.asciichat_shared_init(None, False, False) == 0
args = ["ascii-chat", "--log-level=error"]
args.append("blue-mountain-tiger" if mode == "discovery" else mode)
args += [f"--audio-capture-source={source}", f"--test-audio={test}", f"--microphone-channels={channels}"]
encoded = [arg.encode() for arg in args]
argv = (c.c_char_p * (len(encoded) + 1))(*encoded, None)
assert lib.options_init(len(encoded), argv) == 0
for has_audio in (False, True):
    config = lib.client_audio_pipeline_config_for_source(has_audio)
    media = test == "true" or (has_audio and source in ("auto", "media", "both"))
    stereo = media or channels == "2"
    assert (config.opus_application, config.opus_bitrate) == (
        (2049, 128000) if stereo else (2048, 32000)
    ), (mode, source, test, has_audio)
    assert config.channels == 2
    assert config.encoder_channels == 2
lib.options_state_destroy()
lib.asciichat_shared_destroy()
