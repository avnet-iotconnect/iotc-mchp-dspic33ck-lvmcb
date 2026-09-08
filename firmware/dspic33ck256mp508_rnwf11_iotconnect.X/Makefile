# Manual build for dsPIC33CK256MP508 using XC16 without MPLAB X IDE

PROJECT := bldc
TARGET := $(PROJECT).elf
HEX_TARGET := $(PROJECT).hex
BUILD_DIR := build
CC := xc16-gcc
BIN2HEX := xc16-bin2hex
CPU := 33CK256MP508
CFLAGS := -mcpu=$(CPU) -I. -I./hal -I./diagnostics -I./iotconnect -I./library/library-motor -I./library/library-x2cscope
LDFLAGS := -mcpu=$(CPU) -T /opt/microchip/xc16/v2.10/support/dsPIC33C/gld/p33CK256MP508.gld -Wl,-Map,$(BUILD_DIR)/$(PROJECT).map
SRC := \
	bldc_main.c \
	trap.c \
	iotconnect/iotconnect_rnwf11.c \
	diagnostics/diagnostics_x2cscope.c \
	hal/adc.c \
	hal/clock.c \
	hal/uart1.c \
	hal/sccp.c \
	hal/port_config.c \
	hal/device_config.c \
	hal/pwm.c \
	hal/board_service.c \
	hal/measure.c

OBJS := $(SRC:%.c=$(BUILD_DIR)/%.o)

all: $(BUILD_DIR)/$(HEX_TARGET)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(OBJS) ./library/library-motor/libmotor_control_dspic-elf.a ./library/library-x2cscope/libx2cscope-generic-dspic-elf.a -o $@

$(BUILD_DIR)/$(HEX_TARGET): $(BUILD_DIR)/$(TARGET)
	$(BIN2HEX) $<

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
