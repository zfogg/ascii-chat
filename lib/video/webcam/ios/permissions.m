#import <AVFoundation/AVFoundation.h>
#include <ascii-chat/video/webcam/ios/camera.h>

int asciichat_ios_camera_authorized(void) {
  return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] == AVAuthorizationStatusAuthorized;
}

void asciichat_ios_camera_request_access(void) {
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                           completionHandler:^(BOOL granted) {
                             (void)granted;
                           }];
}
