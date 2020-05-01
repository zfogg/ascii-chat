#include <iostream>
#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <stdio.h>
#include <jpeglib.h>
//#include "ext/fmemopen/fmemopen.h"

#include "webcam.hpp"

using namespace std;
using namespace cv;


vector<uchar> jpegbuf;

vector<int> jpegbuf_params;

VideoCapture camera;

void webcam_init(unsigned short int webcam_index) {
    camera.open(webcam_index);
    if(!camera.isOpened()) {
        perror("Failed to connect to a webcam.");
        exit(1);
    }

    jpegbuf_params.push_back(CV_IMWRITE_JPEG_QUALITY);
    jpegbuf_params.push_back(100);
}


FILE *webcam_read() {
    Mat frame, edges;

    camera >> frame;

    flip(frame, frame, +1);

    cvtColor(frame, edges, CV_BGR2GRAY);

    imencode(".jpg", frame, jpegbuf, jpegbuf_params);

    FILE *jpegfile = fmemopen(jpegbuf.data(), jpegbuf.size()+1, "r");

    // FIXME: do I even need this?
    waitKey(16); // FPS

    return jpegfile;
}
