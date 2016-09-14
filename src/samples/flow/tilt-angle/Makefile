ifeq (y,$(or $(BUILD_TYPE_RELEASE),$(BUILD_TYPE_DEBUG)))
#
# In-tree makefile
#
obj-$(FLOW_TILT_ANGLE_ARCTANGENT_NODE) += arctangent.mod
obj-arctangent-$(FLOW_TILT_ANGLE_ARCTANGENT_NODE) := arctan-spec.json
obj-arctangent-$(FLOW_TILT_ANGLE_ARCTANGENT_NODE) += arctan-node.o
obj-arctangent-$(FLOW_TILT_ANGLE_ARCTANGENT_NODE)-type := flow
obj-arctangent-$(FLOW_TILT_ANGLE_ARCTANGENT_NODE)-extra-ldflags += -lm

sample-$(FLOW_TILT_ANGLE_SAMPLE) += tilt_angle
sample-tilt_angle-$(FLOW_TILT_ANGLE_SAMPLE) := tilt_angle.fbp
else
#
# Out-of-tree makefile
#
## variables share with external
CC ?= gcc
SYSROOT ?= /
LIBDIR ?= /usr/lib/
DESTDIR ?= $(SYSROOT)
SOLINSTALLEDPATH ?= $(SYSROOT)
## variables use locally
DESTDIRNOBACKSLASH = $(patsubst %/,%,$(DESTDIR))
LIBDIRNOFRONTSLASH = $(patsubst /%,%,$(LIBDIR))
DESTLIBPATHNOBACKSLASH = $(patsubst %/,%,$(DESTDIRNOBACKSLASH)/$(LIBDIRNOFRONTSLASH))
SOLINSTALLEDPATHNOBACKSLASH = $(patsubst %/,%,$(SOLINSTALLEDPATH))
SOLLIBPATH = $(SOLINSTALLEDPATHNOBACKSLASH)/$(LIBDIRNOFRONTSLASH)
SOLBINPATH = $(SOLINSTALLEDPATHNOBACKSLASH)/usr/bin/
GENSCHEMA = $(SOLINSTALLEDPATHNOBACKSLASH)/usr/share/soletta/flow/schemas/node-type-genspec.schema
## compiler flags
EXTRA_CFLAGS += -fstack-protector-strong -fPIE -fPIC -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security
EXTRA_LDFLAGS += -z noexecstack -z relro -z now -pie
CFLAGS += --sysroot=$(SYSROOT) -I$(SOLINSTALLEDPATHNOBACKSLASH)/usr/include/soletta/ -DSOL_FLOW_NODE_TYPE_MODULE_EXTERNAL=1 $(EXTRA_CFLAGS)
LDFLAGS += -L$(SOLLIBPATH) -lsoletta -lm $(EXTRA_LDFLAGS)
SOFLAGS = -shared
OBJ = obj
SOLPREFIX := sol_flow_node_type

.PHONY: all install clean
all: arctangent.so

arctan-spec-gen.h arctan-spec-gen.c arctan-spec-gen.json: arctan-spec.json
	$(SOLBINPATH)sol-flow-node-type-gen.py --prefix $(SOLPREFIX) --genspec-schema $(GENSCHEMA) $^ $*.h $*.c $*.json

tilt_angle.c: tilt_angle.fbp arctan-spec-gen.json
	$(SOLBINPATH)sol-fbp-generator -j arctan-spec-gen.json $< $@

$(OBJ)/%.o: %.c arctan-spec-gen.h arctan-spec-gen.c
	mkdir -p $(OBJ)
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)

arctangent.so: $(OBJ)/arctan-node.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOFLAGS) $^ -o $@

tilt_angle: $(OBJ)/tilt_angle.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

install: arctangent.so tilt_angle.fbp README Makefile arctan-spec-gen.h arctan-spec-gen.c arctan-node.c arctan-spec.json arctan-spec-gen.json
	install -d $(DESTLIBPATHNOBACKSLASH)/soletta/modules/flow/
	install -m 0755 $< $(DESTLIBPATHNOBACKSLASH)/soletta/modules/flow/
	install -d $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/
	install -m 0755 tilt_angle.fbp $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/
	install -m 0644 README $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/
	install -m 0644 Makefile $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/
	install -m 0644 arctan-spec.json $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/
	install -m 0644 arctan-node.c $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/
	install -m 0644 arctan-spec-gen.* $(DESTDIRNOBACKSLASH)/opt/soletta/samples/flow/tilt-angle/

clean:
	rm -rf $(OBJ) arctan-spec-gen.* *.so tilt_angle tilt_angle.c
endif