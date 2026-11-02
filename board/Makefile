# A Tock app for a HILShare board. Run ./fetch-dependencies.sh first.

TOCK_USERLAND_BASE_DIR ?= third-party/libtock-c
PACKAGE_NAME := my-app
# The Nucleo-F411RE. The TAB holds cortex-m4.tbf only.
TOCK_TARGETS := cortex-m4
# The Tock release-2.3.0 kernel crate reports version 2.3, and its loader skips an app that
# requires a later version with no message.
KERNEL_MINOR_VERSION := 3
# The MPU block of a process is a power of two and starts at a multiple of half its size, so
# 64 KB is the largest block above the kernel. The kernel takes 1128 bytes of it. Thus,
# minimum_ram_size in the TBF must not be above 64408 bytes, and preflight refuses a larger app.
STACK_SIZE := 16384
APP_HEAP_SIZE := 8192

CXX_SRCS := main.cc
override CXXFLAGS += -fno-exceptions -fno-rtti -fno-threadsafe-statics
override CPPFLAGS += -DHILSHARE_STACK_SIZE=$(STACK_SIZE)
# newlib-nano leaves `%f` out of printf without this symbol. It costs about 8 KB of flash.
override WLFLAGS += -Wl,-u,_printf_float
# The code starts 128 bytes into the TBF for any app name, so a `pc` converts to an ELF address.
ELF2TAB_ARGS += --protected-region-size 128

include $(TOCK_USERLAND_BASE_DIR)/AppMakefile.mk

# libtock-c appends -Os to CPPFLAGS. The last -O flag wins.
override CPPFLAGS += -O2

# Each object file keeps the directory of its source, so two sources can have one file name.
$(shell mkdir -p $(addprefix $(BUILDDIR)/cortex-m4/,$(sort $(dir $(C_SRCS) $(CXX_SRCS)))))
