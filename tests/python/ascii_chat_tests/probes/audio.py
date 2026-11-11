"""Check deterministic audio hash output and uint64 position wrap under sanitizers."""
from pathlib import Path
import ctypes
import os
import sys

def main():
    library = Path(sys.argv[1]).resolve()
    dll_directories = []
    if os.name == "nt":
        for directory in (library.parent, library.parent.parent / "vcpkg_installed/x64-windows/debug/bin",
                          library.parent.parent / "vcpkg_installed/x64-windows/bin"):
            if directory.is_dir():
                dll_directories.append(os.add_dll_directory(str(directory)))

    native = ctypes.CDLL(str(library))
    native.audio_test_pattern.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_size_t,
                                          ctypes.c_uint64, ctypes.c_uint32]
    native.audio_test_pattern.restype = None


    def samples(position, count, rate):
        output = (ctypes.c_float * count)()
        native.audio_test_pattern(output, count, position, rate)
        return list(output)


    # Independent reference values from the 32-bit hash with arithmetic reduced mod 2^32.
    for rate, position, expected in ((44100, 534712, 0.013246911853),
                                     (44100, 584325, 0.014264790046),
                                     (48000, 582000, -0.022427613905),
                                     (48000, 636000, -0.001171497786)):
        actual = samples(position, 1, rate)[0]
        assert abs(actual - expected) < 1e-7, (rate, position, actual, expected)

    for rate in (44100, 48000):
        final_position = (1 << 64) - 1
        assert samples(final_position - 1, 4, rate) == (
            samples(final_position - 1, 2, rate) + samples(0, 2, rate))
        assert samples(12 * rate - 11, 1024, rate) == (
            samples(12 * rate - 11, 11, rate) + samples(12 * rate, 1013, rate))
    print("PASS: noise hash reference samples and uint64 position wrap")


if __name__ == "__main__":
    main()
