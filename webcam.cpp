#include <cstdio>
#include <cstdlib>
#include <opencv2/opencv.hpp>

#include "options.h"
#include "webcam.hpp"
#include "image.h"

using namespace std;
using namespace cv;

VideoCapture camera;

void webcam_init(unsigned short int webcam_index) {
  fprintf(stderr, "Attempting to open webcam with index %d...\n", webcam_index);
  camera.open(webcam_index);
  if (!camera.isOpened()) {
    fprintf(stderr, "Failed to connect to a webcam.\n");
    fprintf(stderr, "On macOS, you may need to grant camera permissions:\n");
    fprintf(stderr, "*. Say \"yes\" to the popup about system camera access that "
                    "you see when running this program for the first time.\n");
    fprintf(stderr, "*. If you said \"no\" to the popup, go to System Preferences "
                    "> Security & Privacy > Privacy > Camera.\n");
    fprintf(stderr, "   Now flip the switch next to your terminal application in that "
                    "privacy list to allow ascii-chat to access your camera.\n");
    fprintf(stderr, "   Then just run this program again.\n");
    exit(1);
  }

  fprintf(stderr, "Webcam opened successfully!\n");
}

image_t *webcam_read() {
  Mat frame;

  camera >> frame;

  // Check if frame was captured successfully
  if (frame.empty()) {
    fprintf(stderr, "Failed to capture frame from webcam\n");
    return NULL;
  }

  if (opt_webcam_flip == 1) {
    flip(frame, frame, +1); // horizontal flip to mirror the image.
  }

  // Convert BGR to RGB (OpenCV uses BGR by default)
  cvtColor(frame, frame, cv::COLOR_BGR2RGB);

  // Create image_t structure
  image_t *img = image_new(frame.cols, frame.rows);
  if (!img) {
    fprintf(stderr, "Failed to allocate image buffer\n");
    return NULL;
  }

  // Copy OpenCV Mat data directly to image_t
  // OpenCV Mat stores data as contiguous RGB bytes
  const size_t data_size =
      static_cast<size_t>(frame.cols) * static_cast<size_t>(frame.rows) * 3; // 3 bytes per RGB pixel

  memcpy(img->pixels, frame.data, data_size);

  return img;
}

void webcam_cleanup() {
  bool was_opened = camera.isOpened();
  if (was_opened) {
    camera.release();
    fprintf(stdout, "Webcam resources released\n");
  } else {
    fprintf(stdout, "Webcam was not opened, nothing to release\n");
  }
}
