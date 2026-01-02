TARGET = main

# Sources
SRCS = main.c startup_stm32f411.s spi.c st7789.c i2s.c dma.c sdcard_spi.c sdcard.c fat32.c encoder.c sequencer_clock.c sequencer.c wav_loader.c audio_mixer.c buttons.c dma_spi.c

# Toolchain
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

# Flags
CFLAGS  = -ggdb -O0 -Wall -Wextra -std=c99
CFLAGS += -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS += -DSTM32F411xE

# Linker Flags
LDFLAGS = -T STM32F411.ld -nostartfiles -Wl,--gc-sections -specs=nano.specs -lc -lnosys -lm

# Rules
all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(TARGET).bin
	dfu-util -a 0 -s 0x08000000:leave -D $(TARGET).bin

clean:
	rm -f *.elf *.bin *.o *.map
