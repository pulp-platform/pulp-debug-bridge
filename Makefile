ifndef INSTALL_DIR
STAND_ALONE_INSTALL=1
endif


INSTALL_DIR ?= $(CURDIR)/install
TARGET_INSTALL_DIR ?= $(CURDIR)/install
BUILD_DIR   ?= $(CURDIR)/build
DEP_SRC_DIR ?= $(BUILD_DIR)/dep_src



HEADER_FILES += $(shell find include -name *.hpp)
TARGET_HEADER_FILES += $(shell find include -name *.h)
HEADER_FILES += $(shell find include -name *.h)
HEADER_FILES += $(shell find bin -type f)

define declareInstallFile
$(INSTALL_DIR)/$(1): $(1)
	install -D $(1) $$@
INSTALL_HEADERS += $(INSTALL_DIR)/$(1)
endef

define declareTargetInstallFile
$(TARGET_INSTALL_DIR)/$(1): $(1)
	install -D $(1) $$@
INSTALL_HEADERS += $(TARGET_INSTALL_DIR)/$(1)
endef

define declareJsonInstallFile
$(INSTALL_DIR)/$(1): json-tools/$(1)
	install -D json-tools/$(1) $$@
INSTALL_HEADERS += $(INSTALL_DIR)/$(1)
endef

HEADER_FILES += $(shell find python -name *.py)

FTDI_CFLAGS = $(shell libftdi1-config --cflags)
FTDI_LDFLAGS = $(shell libftdi1-config --libs)

ifeq '$(FTDI_CFLAGS)$(FTDI_LDFLAGS)' ''
FTDI_CFLAGS = $(shell libftdi-config --cflags)
FTDI_LDFLAGS = $(shell libftdi-config --libs)
endif

ifneq '$(FTDI_CFLAGS)$(FTDI_LDFLAGS)' ''
USE_FTDI=1
endif

CFLAGS += -O3 -g -fPIC -std=gnu++11 -MMD -MP -Isrc -Iinclude -I$(INSTALL_DIR)/include $(FTDI_CFLAGS)
LDFLAGS += -O3 -g -shared $(FTDI_LDFLAGS)

SRCS = src/python_wrapper.cpp src/ioloop.cpp src/cables/jtag.cpp src/reqloop.cpp \
src/cables/adv_dbg_itf/adv_dbg_itf.cpp src/gdb-server/gdb-server.cpp \
src/gdb-server/rsp.cpp src/gdb-server/target.cpp src/gdb-server/breakpoints.cpp

ifdef STAND_ALONE_INSTALL
else
LDFLAGS += $(foreach dir,$(DEP_DIRS),-L$(dir)/lib)
LDFLAGS += -ljson 
endif

ifneq '$(USE_FTDI)' ''
CFLAGS += -D__USE_FTDI__
SRCS += src/cables/ftdi/ftdi.cpp
endif

SRCS += src/cables/jtag-proxy/jtag-proxy.cpp

OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))

$(foreach file, $(HEADER_FILES), $(eval $(call declareInstallFile,$(file))))
$(foreach file, $(TARGET_HEADER_FILES), $(eval $(call declareTargetInstallFile,$(file))))


all: checkout deps build

$(DEP_SRC_DIR)/json-tools:
	mkdir -p $(DEP_SRC_DIR)
	cd $(DEP_SRC_DIR) && git clone https://github.com/pulp-platform/json-tools.git

checkout: $(DEP_SRC_DIR)/json-tools

-include $(OBJS:.o=.d)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(basename $@)
	g++ $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)/libpulpdebugbridge.so: $(OBJS)
	g++ -o $@ $^ $(LDFLAGS)

$(INSTALL_DIR)/lib/libpulpdebugbridge.so: $(BUILD_DIR)/libpulpdebugbridge.so
	install -D $< $@

deps:
	make -C $(DEP_SRC_DIR)/json-tools all BUILD_DIR=$(BUILD_DIR)/json-tools INSTALL_DIR=$(INSTALL_DIR)

build: $(INSTALL_HEADERS) $(INSTALL_DIR)/lib/libpulpdebugbridge.so

clean:
	rm -rf $(BUILD_DIR)
