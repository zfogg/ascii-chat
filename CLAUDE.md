be sure to read and understand the `README.md` and `Makefile` files.

Format code with `make format` after you edit it.

`gdb` doesn't work with this project on macOS for some reason so don't try to 
use it. `lld` works with this project though and you can use that.

always use the `SAFE_MALLOC()` macro from common.h rather than using regular `malloc()`.