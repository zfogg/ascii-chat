#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

void webcam_init(unsigned short int webcam_index);
FILE *webcam_read();
void webcam_cleanup();

#ifdef __cplusplus
}
#endif
