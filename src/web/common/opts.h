#pragma once

#include <emscripten/val.h>

EMSCRIPTEN_DECLARE_VAL_TYPE(OptionsPatch);
int patch_options(OptionsPatch patch, double expected_revision);
int initial_options(OptionsPatch patch);
