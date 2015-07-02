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

ifneq (,$(NOT_FOUND))
warning:
	$(Q)echo -e "The following dependencies were not met:\n"
	$(Q)echo -e $(NOT_FOUND)
	$(Q)echo -e "If you've just installed it, run: make reconf"
	$(Q)echo -e "For more information/options, run: make help"
$(warning-targets)
else
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
all: $(PRE_GEN) $(SOL_LIB_SO) $(SOL_LIB_AR) $(bins-out) $(modules-out)
include $(top_srcdir)tools/build/Makefile.targets
endif # HAVE_KCONFIG_CONFIG
endif # NOT_FOUND

.DEFAULT_GOAL = all
.PHONY = $(PHONY) all

