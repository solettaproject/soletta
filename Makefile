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
ifneq ($(HAVE_KCONFIG_CONFIG),)
-include Makefile.gen
endif

# basic variable definitions
include $(top_srcdir)tools/build/Makefile.vars

include $(top_srcdir)tools/build/Makefile.common

# kconfig interface rules
ifeq (help, $(filter help,$(MAKECMDGOALS)))
help: soletta_help
include $(top_srcdir)tools/build/Makefile.kconfig-proxy
else
include $(top_srcdir)tools/build/Makefile.kconfig
endif

ifeq (,$(filter $(dep-avoid-targets),$(MAKECMDGOALS)))
-include ${all-objs:.o=.o.dep}
endif

ifneq (,$(NOT_FOUND))
warning:
	$(Q)echo -e "The following (required) dependencies were not met:\n"
	$(Q)echo -e "$(NOT_FOUND)"
	$(Q)echo -e "If you've just installed it, run: make reconf"
	$(Q)echo -e "For more information/options, run: make help"
$(warning-targets)
else
ifeq ($(HAVE_KCONFIG_CONFIG),)
warning:
	$(Q)echo "You need a config file first. Please run a config target, e.g.: make menuconfig"
	$(Q)echo "For a quick default config run: make alldefconfig"
	$(Q)echo "For more information/options run: make help"
$(warning-targets)
else
# soletta rules themselves
include $(top_srcdir)tools/build/Makefile.rules

include $(top_srcdir)tools/build/Makefile.targets

default_target: $(PRE_GEN) $(SOL_LIB_OUTPUT) $(bins-out) $(modules-out)
all: default_target
endif # HAVE_KCONFIG_CONFIG
endif # NOT_FOUND

$(KCONFIG_CONFIG): $(KCONFIG_GEN)

.DEFAULT_GOAL = all
.PHONY = $(PHONY) default_target
