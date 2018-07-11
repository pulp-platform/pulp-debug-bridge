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
 * Authors: Andreas Traber
 */

#include "cable.hpp"
#include "gdb-server/gdb-server.hpp"
#include <stdarg.h>

Gdb_server::Gdb_server(Log *log, Cable *cable, js::config *config, int socket_port,
  cmd_cb_t cmd_cb, const char * capabilities)
: log(log), cable(cable), config(config), cmd_cb(cmd_cb), capabilities(capabilities)
{
  target = new Target(this);

  bkp = new Breakpoints(this);

  rsp = new Rsp(this, socket_port);

  if (!rsp->open()) throw std::logic_error("Unable to open RSP server");
}

int Gdb_server::target_is_started() {
  return cmd_cb("__is_started", NULL, 0);
}

void Gdb_server::start_target() {
  cmd_cb("__start_target", NULL, 0);
}

void Gdb_server::stop_target() {
  cmd_cb("__stop_target", NULL, 0);
}

void Gdb_server::target_update_power() {
  target->update_power();
}

void Gdb_server::refresh_target()
{
  target->reinitialize();
  target->update_power();
  bkp->enable_all();
}

int Gdb_server::stop(bool kill)
{
  if (rsp != NULL)
  {
    rsp->close(kill);
    rsp = NULL;
  }
}

void Gdb_server::print(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
}
