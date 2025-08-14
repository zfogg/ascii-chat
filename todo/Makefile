.DEFAULT_GOAL := all

.PHONY: all clean

all:
	$(MAKE) -C . -f Makefile_simd all
	$(MAKE) -C . -f Makefile_rate_limiter all

clean:
	$(MAKE) -C . -f Makefile_simd clean
	$(MAKE) -C . -f Makefile_rate_limiter clean

