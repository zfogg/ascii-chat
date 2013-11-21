.PHONY: default all clean

TARGETS = bin/server bin/client



default: $(TARGETS)
all: default

bin/%:
	$(MAKE) -C $(*F)

clean:
	$(MAKE) -C server clean
	$(MAKE) -C client clean

