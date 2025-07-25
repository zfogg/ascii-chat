#include <opencv2/opencv.hpp>

#include <cstdio>
#include <print>
#include <jpeglib.h>

#include "ext/fmemopen/fmemopen.h"
#include "webcam.hpp"
#include "options.h"


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
        std::println(stderr, "*. Say \"yes\" to the popup about system camera access that you see when running this program for the first time.\n");
        std::println(stderr, "*. If you said \"no\" to the popup, go to System Preferences > Security & Privacy > Privacy > Camera.\n");
        std::println(stderr, "   Now flip the switch next to your terminal application in that privacy list to allow ascii-chat to access your camera.\n");
        std::println(stderr, "   Then just run this program again.\n");
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

    if (opt_webcam_flip == 1) {
        flip(frame, frame, +1); // horizontal flip to mirror the image.
    }

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

