ifndef PULP_SDK_WS_INSTALL
ifdef PULP_SDK_HOME
PULP_SDK_WS_INSTALL = $(PULP_SDK_HOME)/install/ws
else
PULP_SDK_WS_INSTALL = $(CURDIR)/install
endif
STAND_ALONE_INSTALL=1
endif



HEADER_FILES += $(shell find include -name *.hpp)
HEADER_FILES += $(shell find include -name *.h)
HEADER_FILES += $(shell find bin -type f)

define declareInstallFile
$(PULP_SDK_WS_INSTALL)/$(1): $(1)
	install -D $(1) $$@
INSTALL_HEADERS += $(PULP_SDK_WS_INSTALL)/$(1)
endef

define declareJsonInstallFile
$(PULP_SDK_WS_INSTALL)/$(1): json-tools/$(1)
	install -D json-tools/$(1) $$@
INSTALL_HEADERS += $(PULP_SDK_WS_INSTALL)/$(1)
endef

HEADER_FILES += $(shell find python -name *.py)

BUILD_DIR = build

FTDI_CFLAGS = $(shell libftdi1-config --cflags)
FTDI_LDFLAGS = $(shell libftdi1-config --libs)

ifneq '$(FTDI_CFLAGS)$(FTDI_LDFLAGS)' ''
USE_FTDI=1
endif

CFLAGS += -O3 -g -fPIC -std=gnu++11 -MMD -MP -Isrc -Iinclude -I$(PULP_SDK_WS_INSTALL)/include $(FTDI_CFLAGS)
LDFLAGS += -O3 -g -shared $(FTDI_LDFLAGS)

SRCS = src/python_wrapper.cpp src/ioloop.cpp src/cables/jtag.cpp \
src/cables/adv_dbg_itf/adv_dbg_itf.cpp src/gdb-server/gdb-server.cpp \
src/gdb-server/rsp.cpp src/gdb-server/target.cpp

ifdef STAND_ALONE_INSTALL
CFLAGS += -Ijson-tools/include -Ihal/include
SRCS += json-tools/src/jsmn.cpp json-tools/src/json.cpp
JSON_HEADER_FILES += $(shell cd json-tools && find python -name *.py)
else
LDFLAGS += -L$(PULP_SDK_WS_INSTALL)/lib -ljson 
endif

ifneq '$(USE_FTDI)' ''
CFLAGS += -D__USE_FTDI__
SRCS += src/cables/ftdi/ftdi.cpp
endif

SRCS += src/cables/jtag-proxy/jtag-proxy.cpp

OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))

$(foreach file, $(HEADER_FILES), $(eval $(call declareInstallFile,$(file))))
$(foreach file, $(JSON_HEADER_FILES), $(eval $(call declareJsonInstallFile,$(file))))


all: build

checkout:
	git submodule update --init

-include $(OBJS:.o=.d)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(basename $@)
	g++ $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)/libpulpdebugbridge.so: $(OBJS)
	g++ -o $@ $^ $(LDFLAGS)

$(PULP_SDK_WS_INSTALL)/lib/libpulpdebugbridge.so: $(BUILD_DIR)/libpulpdebugbridge.so
	install -D $< $@

build: $(INSTALL_HEADERS) $(PULP_SDK_WS_INSTALL)/lib/libpulpdebugbridge.so

clean:
	rm -rf $(BUILD_DIR)
