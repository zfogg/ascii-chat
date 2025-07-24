#include <opencv2/opencv.hpp>

#include <cstdio>
#include <print>
#include <jpeglib.h>

#include "ext/fmemopen/fmemopen.h"
#include "webcam.hpp"


using namespace std;
using namespace cv;


vector<uchar> jpegbuf;

vector<int> jpegbuf_params;

VideoCapture camera;


void webcam_init(unsigned short int webcam_index) {
    std::println(stderr, "Attempting to open webcam with index {}...", webcam_index);
    camera.open(webcam_index);
    if(!camera.isOpened()) {
        std::println(stderr, "Failed to connect to a webcam.\n");
        std::println(stderr, "On macOS, you may need to grant camera permissions:\n");
        std::println(stderr, "1. Go to System Preferences > Security & Privacy > Privacy > Camera\n");
        std::println(stderr, "2. Add your terminal application (Terminal.app or iTerm2) to the list\n");
        exit(1);
    }
    
    std::println(stderr, "Webcam opened successfully!\n");

    jpegbuf_params.push_back(IMWRITE_JPEG_QUALITY);
    jpegbuf_params.push_back(100);
}


FILE *webcam_read() {
    Mat frame, edges;

    camera >> frame;
    
    // Check if frame was captured successfully
    if (frame.empty()) {
        std::println(stderr, "Failed to capture frame from webcam\n");
        return NULL;
    }

    // flip(frame, frame, +1); // horizontal flip to mirror the image. do we want it?

    cvtColor(frame, edges, cv::COLOR_BGR2GRAY);

    imencode(".jpg", frame, jpegbuf, jpegbuf_params);

    FILE *jpegfile = fmemopen(jpegbuf.data(), jpegbuf.size(), "r");

    // FIXME: do I even need this?
    // waitKey(16); // FPS
    // waitKey(1); // FPS

    return jpegfile;
}

void webcam_cleanup() {
    bool was_opened = camera.isOpened();
    if (was_opened) {
        camera.release();
        std::println(stdout, "Webcam resources released\n");
    } else {
        std::println(stdout, "Webcam was not opened, nothing to release\n");
    }
}
