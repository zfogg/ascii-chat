#pragma once

#include <stdint.h>
#include <stdbool.h>

// Standard C headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// System headers needed by tests
#include <unistd.h>   // For unlink(), access(), etc.
#include <sys/stat.h> // For stat(), struct stat

// Now include Criterion which will pull in system headers
#include <criterion/criterion.h>

// Project headers - use relative paths from tests directory
#include "../lib/common.h"
#include "../lib/tests/logging.h"