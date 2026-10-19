# libfacedetection

Vendored from https://github.com/ShiqiYu/libfacedetection at
`acf7b254121927e7dced30e233a2e03119f28ea2` (BSD-3-Clause; see LICENSE).
The four upstream source files include the pretrained YuNet model as C++ arrays.
`facedetection_export.h` is our static-build export definition. The upstream
sources are otherwise unmodified. OpenCV and OpenMP are not required.

Native and browser builds use the same sources and model. Architecture-specific
instructions are not enabled globally, preserving the supported CPU baseline.
