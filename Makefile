.PHONY: default all clean

WEBCAM   := webcam
ASCII    := ascii
CLIENT   := client
SERVER   := server

BIN_DIR  := bin

WEBCAM_  := $(wildcard $(WEBCAM)/*.cpp) $(wildcard $(WEBCAM)/*.h)
ASCII_   := $(wildcard $(ASCII)/*.c)    $(wildcard $(ASCII)/*.h)
CLIENT_  := $(wildcard $(CLIENT)/*.c)   $(wildcard $(CLIENT)/*.h)
SERVER_  := $(wildcard $(SERVER)/*.c)   $(wildcard $(SERVER)/*.h)

TARGETS  := $(addprefix $(BIN_DIR)/, $(CLIENT) $(SERVER))
TARGETS_ := $(WEBCAM)/$(WEBCAM).o $(ASCII)/$(ASCII).o


.PHONY: all

default: $(TARGETS)

all: default


$(BIN_DIR)/$(SERVER): $(TARGETS_) $(SERVER_)
	$(MAKE) -C $(SERVER)

$(BIN_DIR)/$(CLIENT): $(TARGETS_) $(CLIENT_)
	$(MAKE) -C $(CLIENT)

$(ASCII)/%.o: $(ASCII_)
	$(MAKE) -C $(*F)

$(WEBCAM)/%.o: $(WEBCAM_)
	$(MAKE) -C $(*F)


clean:
	$(MAKE) -C webcam clean
	$(MAKE) -C ascii  clean
	$(MAKE) -C server clean
	$(MAKE) -C client clean
