#include <iostream>
#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <stdio.h>
#include <jpeglib.h>

#include "webcam.hpp"

using namespace std;
using namespace cv;


vector<uchar> jpegbuf;

vector<int> jpegbuf_params;

VideoCapture camera(0);


#ifdef __cplusplus
extern "C" {
#endif

void webcam_init() {
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

    cvtColor(frame, edges, CV_BGR2GRAY);

    imencode(".jpg", frame, jpegbuf, jpegbuf_params);

    FILE *jpegfile = fmemopen(jpegbuf.data(), jpegbuf.size(), "r");

    waitKey(20); // FIXME: do I even need this?

    return jpegfile;
}


#ifdef __cplusplus
}
#endif

