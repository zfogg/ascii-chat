#pragma once

// Mock webcam header for testing without real hardware
// Usage: #include "tests/mocks/webcam_mock.h" BEFORE including webcam.h
//        This will override all webcam functions with mock versions

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "image2ascii/image.h"

// Forward declare the webcam context type
typedef struct webcam_context_t webcam_context_t;

// Mock function declarations (match webcam.h signatures)
int mock_webcam_init(unsigned short int webcam_index);
image_t *mock_webcam_read(void);
void mock_webcam_cleanup(void);

int mock_webcam_init_context(webcam_context_t **ctx, unsigned short int device_index);
void mock_webcam_cleanup_context(webcam_context_t *ctx);
image_t *mock_webcam_read_context(webcam_context_t *ctx);
int mock_webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height);

// Configuration functions for the mock
void mock_webcam_set_video_file(const char *path);
void mock_webcam_set_test_pattern(bool enable);
void mock_webcam_set_dimensions(int width, int height);
void mock_webcam_reset(void);

// Override the real webcam functions with mocks
#define webcam_init mock_webcam_init
#define webcam_read mock_webcam_read
#define webcam_cleanup mock_webcam_cleanup
#define webcam_init_context mock_webcam_init_context
#define webcam_cleanup_context mock_webcam_cleanup_context
#define webcam_read_context mock_webcam_read_context
#define webcam_get_dimensions mock_webcam_get_dimensions

// Prevent the real webcam.c from being compiled
#define WEBCAM_MOCK_ENABLED 1