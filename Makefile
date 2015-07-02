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

# soletta rules themselves
include $(top_srcdir)tools/build/Makefile.rules

# kconfig interface rules
include $(top_srcdir)tools/build/Makefile.kconfig

ifeq (n,$(HAVE_DEPENDENCY_FILES))
PRE_GEN += $(DEPENDENCY_FILES)
endif

ifeq ($(HAVE_KCONFIG_CONFIG),)
warning:
	$(Q)echo "You need a config file first. Please run a config target, e.g.: make menuconfig"
	$(Q)echo "For a quick default config run: make alldefconfig"
	$(Q)echo "For more information/options run: make help"
$(warning-targets)
else
ifeq (n,$(HAVE_PYTHON_JSONSCHEMA))
warning:
	$(Q)echo "Cannot proceed, python module \"jsonschema\" was not found in your system..."
	$(Q)echo "If you've just installed it, run: make reconf"
$(warning-targets)
else
include $(top_srcdir)tools/build/Makefile.targets
all: $(PRE_GEN) $(SOL_LIB_SO) $(SOL_LIB_AR) $(bins-out) $(modules-out)
endif # HAVE_PYTHON_JSONSCHEMA
endif # HAVE_KCONFIG_CONFIG

.DEFAULT_GOAL = all
.PHONY = $(PHONY) all
