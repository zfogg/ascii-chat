#include "marker.h"

#ifndef PROJECT_SETTINGS_PROBE
#error Project options were not applied
#endif

int main(void) {
  return PROJECT_SETTINGS_MARKER != PROJECT_SETTINGS_PROBE;
}
