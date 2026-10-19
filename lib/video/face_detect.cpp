#include <ascii-chat/video/face_follow.h>
#include "facedetectcnn.h"

extern "C" void face_follow_detect(unsigned char *results, unsigned char *bgr, int width, int height,
                                   face_follow_detection_t *faces, size_t capacity, size_t *count) {
  *count = 0;
  int *detections = facedetect_cnn(results, bgr, width, height, width * 3);
  if (!detections)
    return;
  for (int i = 0; i < detections[0] && *count < capacity; i++) {
    // The upstream format is an int count followed by 16-short records.
    const unsigned char *record = results + sizeof(int) + static_cast<size_t>(i) * 16 * sizeof(short);
    const short *face = reinterpret_cast<const short *>(record);
    faces[*count] = {{static_cast<float>(face[1]) / width, static_cast<float>(face[2]) / height,
                      static_cast<float>(face[3]) / width, static_cast<float>(face[4]) / height},
                     static_cast<float>(face[0]) / 100.0f};
    ++*count;
  }
}
