top_srcdir = ./
MAKEFLAGS += -r --no-print-directory

# kconfig settings
KCONFIG_CONFIG ?= $(top_srcdir).config
KCONFIG_INCLUDE := $(top_srcdir)include/
KCONFIG_AUTOHEADER := $(KCONFIG_INCLUDE)generated/sol_config.h
HAVE_KCONFIG_CONFIG := $(if $(wildcard $(KCONFIG_CONFIG)),y,)
export KCONFIG_AUTOHEADER

-include $(KCONFIG_CONFIG)

# dependency-resolver generated, deps resolution
-include Makefile.gen

# basic variable definitions
include $(top_srcdir)tools/build/Makefile.vars

ifeq (n,$(HAVE_DEPENDENCY_FILES))
build := $(DEPENDENCY_FILES)
BUILD_SOLETTA := n
else

ifeq ($(HAVE_KCONFIG_CONFIG),)
build := config_warning
BUILD_SOLETTA := n

config_warning:
	$(Q)echo "Need a config file first, run a config target, i.e: make menuconfig"
	$(Q)echo "For a quick default config run: make alldefconfig"
	$(Q)echo "For more information/options run: make help"
else

ifeq (n,$(HAVE_PYTHON_JSONSCHEMA))
BUILD_SOLETTA := n
build := config_warning
config_warning:
	$(Q)echo "Cannot proceed, python module \"jsonschema\" was not found in your system..."

else
build := $(PRE_GEN)
BUILD_SOLETTA := y
endif # HAVE_PYTHON_JSONSCHEMA
endif # HAVE_KCONFIG_CONFIG
endif # HAVE_DEPENDENCY_FILES

# kconfig interface rules
include $(top_srcdir)tools/build/Makefile.kconfig

# soletta rules itself
include $(top_srcdir)tools/build/Makefile.rules

ifeq (y,$(BUILD_SOLETTA))
build += $(SOL_LIB_SO) $(SOL_LIB_AR) $(bins-out) $(modules-out)
endif

all: $(build)

.DEFAULT_GOAL = all
.PHONY = $(PHONY) all
