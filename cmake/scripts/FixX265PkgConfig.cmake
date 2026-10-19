# FFmpeg supplies the target's C++ runtime explicitly in its cross link probes.
file(READ "${PREFIX}/lib/pkgconfig/x265.pc" _contents)
string(REPLACE "-lstdc++" "" _contents "${_contents}")
file(WRITE "${PREFIX}/lib/pkgconfig/x265.pc" "${_contents}")
