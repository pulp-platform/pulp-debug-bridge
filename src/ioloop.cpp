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
#include <atomic>
#include "cable.hpp"
#include "debug_bridge/debug_bridge.h"
#include <unistd.h>

#define PTR_2_INT(__addr) ((unsigned int)(reinterpret_cast<std::uintptr_t>(__addr)&0xffffffff))
#define INT_2_PTR(__addr) (reinterpret_cast<std::uintptr_t>((size_t)__addr))

#define DEFAULT_LOOP_DELAY 50000
#define DEFAULT_SLOW_LOOP_DELAY 250000

class Ioloop
{
public:
  Ioloop(Cable *cable, unsigned int debug_struct_addr);
  void ioloop_routine();
  int stop(bool kill);
  hal_debug_struct_t *activate();
  void set_poll_delay(int delay);

private:
  std::thread *thread;
  Cable *cable;
  bool end = false;
  std::mutex joined_mutex;
  bool joined = false;
  unsigned int debug_struct_addr;
  int status = 0;
  std::atomic<int> delay;
  Log *log;
};

int Ioloop::stop(bool kill)
{
  log->debug("ioloop: stop (kill: %d)\n", kill);
  if (thread) {
    if (kill) end = true;
    {
      std::lock_guard<std::mutex> guard(joined_mutex);
      if (joined||!thread->joinable()) {
        log->debug("ioloop: stop (kill: %d) completed (already joined or not joinable)\n", kill);
        return status;
      }
      joined = true;
    }
    log->debug("ioloop: stop (kill: %d) joining thread\n", kill);
    thread->join();
    delete(thread);
    thread = NULL;
  }
  log->debug("ioloop: stop (kill: %d) completed\n", kill);
  return status;
}

hal_debug_struct_t *Ioloop::activate()
{
  hal_debug_struct_t *debug_struct = NULL;
  while(!(end||debug_struct)) {

    if (!cable->access(false, debug_struct_addr, 4, (char*)&debug_struct))
      return NULL;

    if (debug_struct != NULL) {
      // The binary has just started, we need to tell him we want to watch for printf
      unsigned int value = 0;
      if (!cable->access(true, PTR_2_INT(&debug_struct->use_internal_printf), 4, (char*)&value))
        return NULL;
    }

    // We use a fast loop to miss as few printf as possible as the binary will
    // start without waiting for any acknolegment in case
    // no host loader is connected
    usleep(1);
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
      debug_struct = activate();

      if (end) break;

      // First check if the application has exited
      if (!cable->access(false, PTR_2_INT(&debug_struct->exit_status), 4, (char*)&value)) {
        end = true;
        break;
      }

      if (value >> 31) {
        status = ((int)value << 1) >> 1;
        log->user("Detected end of application, exiting with status: %d\n", status);
        end = true;
        break;
      }

      // Check printf
      // The target application should quickly dumps the characters, so we can loop on printf
      // until we don't find anything
      while(!end) {
        if (!cable->access(false, PTR_2_INT(&debug_struct->pending_putchar), 4, (char*)&value)) {
          end = true;
          break;
        }
        if (value == 0) break;
        std::vector<char> buff(value + 1);
        if (!cable->access(false, PTR_2_INT(&debug_struct->putc_buffer), value, &(buff[0]))) {
          end = true;
          break;
        }
        unsigned int zero = 0;
        if (!cable->access(true, PTR_2_INT(&debug_struct->pending_putchar), 4, (char*)&zero)) {
          end = true;
          break;
        }
        for (unsigned int i=0; i<value; i++) putchar(buff[i]);
        fflush(NULL);
      }

      if (end) break;
      
      // Small sleep to not poll too often
      int total_delay = delay;
      while (!end && total_delay > 0){
        usleep(DEFAULT_LOOP_DELAY);
        total_delay -= DEFAULT_LOOP_DELAY;
      }
    }
  }
  log->debug("ioloop thread exited\n");
}

void Ioloop::set_poll_delay(int delay)
{
  log->debug("ioloop delay set to %d\n", delay);
  this->delay = delay;
}

Ioloop::Ioloop(Cable *cable, unsigned int debug_struct_addr) : cable(cable),
  debug_struct_addr(debug_struct_addr), delay(DEFAULT_LOOP_DELAY), log(new Log())
{
  // activate();
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

extern "C" void bridge_ioloop_set_poll_delay(void *arg, int high_rate)
{
  Ioloop *ioloop = (Ioloop *)arg;
  if (high_rate)
    ioloop->set_poll_delay(DEFAULT_LOOP_DELAY);
  else
    ioloop->set_poll_delay(DEFAULT_SLOW_LOOP_DELAY);

}


