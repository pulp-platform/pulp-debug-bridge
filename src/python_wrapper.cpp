/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <stdexcept>

#include "json.hpp"
#include "cables/log.h"
#include "cables/adv_dbg_itf/adv_dbg_itf.hpp"
#include "cables/jtag-proxy/jtag-proxy.hpp"
#ifdef __USE_FTDI__
#include "cables/ftdi/ftdi.hpp"
#endif

using namespace std;


static int bridge_verbose = 0;
static const char *bridge_error = NULL;

class MyLogIF : public LogIF {
  public:
    void error(const char *str, ...) ;
    void warning(const char *str, ...) ;
    void user(const char *str, ...) ;
    void debug(const char *str, ...) ;
};

void MyLogIF::user(const char *str, ...)
{
  if (!bridge_verbose) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void MyLogIF::debug(const char *str, ...)
{
  if (!bridge_verbose) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void MyLogIF::warning(const char *str, ...)
{
  if (!bridge_verbose) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void MyLogIF::error(const char *str, ...)
{
  char buff[1024];
  va_list va;
  va_start(va, str);
  vsnprintf(buff, 1024, str, va);
  va_end(va);
  bridge_error = strdup(buff);

  if (!bridge_verbose) return;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

extern "C" void *cable_new(const char *config_string)
{
  const char *cable_name = NULL;
  js::config *config = NULL;

  if (config_string != NULL)
  {
    config = js::import_config_from_string(std::string(config_string));
    js::config *type_config = config->get("type");
    if (type_config != NULL)
    {
      cable_name = type_config->get_str().c_str();
    }
  }

  if (cable_name == NULL) {
    bridge_error = "No cable name specified";
    return NULL;
  }

  if (strncmp(cable_name, "ftdi", 4) == 0)
  {
#ifdef __USE_FTDI__
    MyLogIF *log = new MyLogIF();
    Ftdi::FTDIDeviceID id = Ftdi::Olimex;
    if (strcmp(cable_name, "ftdi@digilent") == 0) id = Ftdi::Digilent;
    Adv_dbg_itf *adu = new Adv_dbg_itf(log, new Ftdi(log, id));
    if (!adu->connect(config)) return NULL;
    int tap = 0;
    if (config->get("tap")) tap = config->get("tap")->get_int();
    adu->device_select(tap);
    return (void *)static_cast<Cable *>(adu);
#else
    fprintf(stderr, "Debug bridge has not been compiled with FTDI support\n");
    return NULL;
#endif
  }
  else if (strcmp(cable_name, "jtag-proxy") == 0)
  {
    MyLogIF *log = new MyLogIF();
    Adv_dbg_itf *adu = new Adv_dbg_itf(log, new Jtag_proxy(log));
    if (!adu->connect(config)) return NULL;
    int tap = 0;
    if (config->get("tap")) tap = config->get("tap")->get_int();
    adu->device_select(tap);
    return (void *)static_cast<Cable *>(adu);
  }
  else
  {
    fprintf(stderr, "Unknown cable: %s\n", cable_name);
    return NULL;
  }
  
  return NULL;
}

extern "C" void cable_write(void *cable, unsigned int addr, int size, const char *data)
{
  Adv_dbg_itf *adu = (Adv_dbg_itf *)cable;
  adu->access(true, addr, size, (char *)data);
}

extern "C" void cable_read(void *cable, unsigned int addr, int size, const char *data)
{
  Adv_dbg_itf *adu = (Adv_dbg_itf *)cable;
  adu->access(false, addr, size, (char *)data);
}

extern "C" void chip_reset(void *handler, bool active)
{
  Adv_dbg_itf *cable = (Adv_dbg_itf *)handler;
  cable->chip_reset(active);
}

extern "C" void jtag_reset(void *handler, bool active)
{
  Adv_dbg_itf *cable = (Adv_dbg_itf *)handler;
  cable->jtag_reset(active);
}

extern "C" void jtag_soft_reset(void *handler)
{
  Adv_dbg_itf *cable = (Adv_dbg_itf *)handler;
  cable->jtag_soft_reset();
}


extern "C" bool cable_jtag_set_reg(void *handler, unsigned int reg, int width, unsigned int value)
{
  Cable *cable = (Cable *)handler;
  return cable->jtag_set_reg(reg, width, value);
}

extern "C" bool cable_jtag_get_reg(void *handler, unsigned int reg, int width, unsigned int *out_value, unsigned int value)
{
  Cable *cable = (Cable *)handler;
  return cable->jtag_get_reg(reg, width, out_value, value);
}




static void init_sigint_handler(int s) {
  raise(SIGTERM);
}

extern "C" char * bridge_get_error()
{
  if (bridge_error == NULL) return strdup("unknown error");
  return strdup(bridge_error);
}

extern "C" void bridge_init(int verbose)
{
  bridge_verbose = verbose;

  // This should be the first C method called by python.
  // As python is not catching SIGINT where we are in C world, we have to
  // setup a  sigint handler to exit in case control+C is hit.
  signal (SIGINT, init_sigint_handler);

}

#if 0

extern "C" void plt_exit(void *_bridge, bool status)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getMemIF()->exit(status);
}

extern "C" bool jtag_reset(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_reset();
}

extern "C" bool jtag_idle(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_idle();
}

extern "C" bool jtag_shift_ir(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_shift_ir();
}

extern "C" bool jtag_shift_dr(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_shift_dr();
}

extern "C" void jtag_shift(void *_bridge, int width, const char *datain, const char *dataout, int noex)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_shift(width, (unsigned char *)datain, (unsigned char *)dataout, noex);
}

#endif
