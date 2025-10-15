# 这个文件负责公共的配置

# 允许外部覆盖前缀；默认用裸机工具链
CROSS_COMPILE ?= riscv64-unknown-elf-

TOOLPREFIX := $(CROSS_COMPILE)

override CC      := $(TOOLPREFIX)gcc
override LD      := $(TOOLPREFIX)ld
override OBJCOPY := $(TOOLPREFIX)objcopy
override OBJDUMP := $(TOOLPREFIX)objdump

export CROSS_COMPILE CC LD OBJCOPY OBJDUMP


# 编译相关配置
CFLAGS = -Wall  -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# 链接相关配置 
LDFLAGS = -z max-page-size=4096