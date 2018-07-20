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
#include <thread>
#include <mutex>
#include <condition_variable>
#include "cable.hpp"
#include "debug_bridge/debug_bridge.h"
#include <unistd.h>


class Ioloop
{
public:
  Ioloop(Cable *cable, unsigned int debug_struct_addr);
  void ioloop_routine();
  int stop(bool kill);
  hal_debug_struct_t *activate();

private:
  std::thread *thread;
  Cable *cable;
  bool end = false;
  unsigned int debug_struct_addr;
  int status;
};

int Ioloop::stop(bool kill)
{
  if (thread) {
    if (kill) end = true;
    if (thread->joinable()) {
      thread->join();
    }
    delete(thread);
    thread = NULL;
  }
  return status;
}

hal_debug_struct_t *Ioloop::activate()
{
  hal_debug_struct_t *debug_struct = NULL;

  cable->access(false, debug_struct_addr, 4, (char*)&debug_struct);

  if (debug_struct != NULL) {
    // The binary has just started, we need to tell him we want to watch for printf
    unsigned int value = 0;
    cable->access(true, (unsigned int)(long)&debug_struct->use_internal_printf, 4, (char*)&value);
  }

  return debug_struct;
}

void Ioloop::ioloop_routine()
{
  if (debug_struct_addr) {

    // In case the debug struct pointer is found, iterate to receive IO requests
    // from runtime
    while(!end)
    {

      hal_debug_struct_t *debug_struct = NULL;

      uint32_t value;

      // debugStruct_ptr is used to synchronize with the runtime at the first start or 
      // when we switch from one binary to another
      // Each binary will initialize when it boots will wait until we set it to zero before stopping
      while(1) {
        if ((debug_struct = activate()) != NULL) break;

        // We use a fast loop to miss as few printf as possible as the binary will
        // start without waiting for any acknolegment in case
        // no host loader is connected
        usleep(1);
      }

      // First check if the application has exited
      cable->access(false, (unsigned int)(long)&debug_struct->exit_status, 4, (char*)&value);
      if (value >> 31) {
        status = ((int)value << 1) >> 1;
        printf("Detected end of application, exiting with status: %d\n", status);
        return;
      }

      // Check printf
      // The target application should quickly dumps the characters, so we can loop on printf
      // until we don't find anything
      while(1) {
        cable->access(false, (unsigned int)(long)&debug_struct->pending_putchar, 4, (char*)&value);
        if (value == 0) break;
        char buff[value+1];
        cable->access(false, (unsigned int)(long)&debug_struct->putc_buffer, value, (char*)buff);
        unsigned int zero = 0;
        cable->access(true, (unsigned int)(long)&debug_struct->pending_putchar, 4, (char*)&zero);
        for (uint i=0; i<value; i++) putchar(buff[i]);
        fflush(NULL);
      }

      // Small sleep to not poll too often
      usleep(50000);
    }
  }
}

Ioloop::Ioloop(Cable *cable, unsigned int debug_struct_addr) : cable(cable), debug_struct_addr(debug_struct_addr)
{
  activate();
  thread = new std::thread(&Ioloop::ioloop_routine, this);
}

extern "C" void *bridge_ioloop_open(void *cable, unsigned int debug_struct_addr)
{
  return (void *)new Ioloop((Cable *)cable, debug_struct_addr);
}

extern "C" int bridge_ioloop_close(void *arg, int kill)
{
  Ioloop *ioloop = (Ioloop *)arg;
  int status = ioloop->stop(kill);
  delete(ioloop);
  return status;
}


