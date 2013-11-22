.PHONY: default all clean

TARGETS = webcam/webcam.o ascii/ascii.o bin/server bin/client


default: $(TARGETS)
all: default

bin/%:
	$(MAKE) -C $(*F)

ascii/%.o:
	$(MAKE) -C $(*F)

webcam/%.o:
	$(MAKE) -C $(*F)

clean:
	$(MAKE) -C webcam clean
	$(MAKE) -C ascii  clean
	$(MAKE) -C server clean
	$(MAKE) -C client clean
