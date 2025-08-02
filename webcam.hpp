#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "image.h"

void webcam_init(unsigned short int webcam_index);
image_t *webcam_read();
void webcam_cleanup();

#ifdef __cplusplus
}
#endif
