.PHONY: default all clean

TARGETS = build/ascii.o bin/server bin/client


default: $(TARGETS)
all: default

bin/%:
	$(MAKE) -C $(*F)

build/%.o:
	$(MAKE) -C $(*F)

clean:
	$(MAKE) -C ascii  clean
	$(MAKE) -C server clean
	$(MAKE) -C client clean
