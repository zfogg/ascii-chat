# =============================================================================
# Module 5: Video Processing (changes weekly)
# =============================================================================
# Video capture, processing, and ASCII rendering

set(VIDEO_SRCS
    lib/video/video_frame.c
    lib/video/image.c
    lib/video/ascii.c
    lib/video/ansi_fast.c
    lib/video/ansi.c
    lib/video/palette.c
    lib/util/utf8.c
    lib/video/webcam/webcam.c
)

# Platform-specific webcam sources already added to PLATFORM_SRCS
# (webcam_avfoundation.m, webcam_v4l2.c, webcam_mediafoundation.c)
