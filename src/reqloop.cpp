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
#include "cable.hpp"
#include "cables/log.h"
#include "hal/debug_bridge/debug_bridge.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


class Reqloop
{
public:
  Reqloop(Cable *cable, unsigned int debug_struct_addr);
  void reqloop_routine();
  int stop(bool kill);
  hal_debug_struct_t *activate();

private:
  void reply_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_connect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);

  Log *log;
  std::thread *thread;
  Cable *cable;
  bool end = false;
  unsigned int debug_struct_addr;
  int status = 0;
};

int Reqloop::stop(bool kill)
{
  if (kill) end = true;
  thread->join();
  return status;
}

hal_debug_struct_t *Reqloop::activate()
{
  hal_debug_struct_t *debug_struct = NULL;

  cable->access(false, debug_struct_addr, 4, (char*)&debug_struct);

  if (debug_struct != NULL) {
    // The binary has just started, we need to tell him we want to watch for requests
    unsigned int value = 0;

    uint32_t connected = 1;
    cable->access(true, (unsigned int)(long)&debug_struct->bridge_connected, 4, (char*)&connected);
  }

  return debug_struct;
}



void Reqloop::reply_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req)
{
  uint32_t value = 1;
  this->cable->access(true, (unsigned int)(long)&target_req->done, sizeof(target_req->done), (char*)&value);

  uint32_t notif_req_addr;
  uint32_t notif_req_value;
  cable->access(false, (unsigned int)(long)&debug_struct->notif_req_addr, 4, (char*)&notif_req_addr);
  cable->access(false, (unsigned int)(long)&debug_struct->notif_req_value, 4, (char*)&notif_req_value);

  cable->access(true, (unsigned int)(long)notif_req_addr, 4, (char*)&notif_req_value);
}

static int transpose_code(int code)
{
  int alt = 0;

  if ((code & 0x0) == 0x0) alt |= O_RDONLY;
  if ((code & 0x1) == 0x1) alt |= O_WRONLY;
  if ((code & 0x2) == 0x2) alt |= O_RDWR;
  if ((code & 0x8) == 0x8) alt |= O_APPEND;
  if ((code & 0x200) == 0x200) alt |= O_CREAT;
  if ((code & 0x400) == 0x400) alt |= O_TRUNC;
  if ((code & 0x800) == 0x800) alt |= O_EXCL;
  if ((code & 0x2000) == 0x2000) alt |= O_SYNC;
  if ((code & 0x4000) == 0x4000) alt |= O_NONBLOCK;
  if ((code & 0x8000) == 0x8000) alt |= O_NOCTTY;

  return alt;
}

bool Reqloop::handle_req_connect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  this->reply_req(debug_struct, target_req, req);
  return true;
}

bool Reqloop::handle_req_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char name[req->open.name_len+1];
  cable->access(false, (unsigned int)(long)req->open.name, req->open.name_len+1, (char*)name);

  int res = open(name, req->open.flags, req->open.mode);

  cable->access(true, (unsigned int)(long)&target_req->open.retval, 4, (char*)&res);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char buffer[4096];
  int size = req->read.len;
  char *ptr = (char *)(long)req->read.ptr;
  int res = 0;
  while (size)
  {
    int iter_size = size;
    if (iter_size > 4096)
      iter_size = 4096;

    iter_size = read(req->read.file, (void *)buffer, iter_size);

    if (iter_size <= 0) {
      if (iter_size == -1 && res == 0) res = -1;
      break;
    }

    cable->access(true, (unsigned int)(long)ptr, iter_size, (char*)buffer);

    res += iter_size;
    ptr += iter_size;
    size -= iter_size;
  }

  cable->access(true, (unsigned int)(long)&target_req->read.retval, 4, (char*)&res);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char buffer[4096];
  int size = req->write.len;
  char *ptr = (char *)(long)req->write.ptr;
  int res = 0;
  while (size)
  {
    int iter_size = size;
    if (iter_size > 4096)
      iter_size = 4096;

    cable->access(false, (unsigned int)(long)ptr, iter_size, (char*)buffer);

    iter_size = write(req->write.file, (void *)buffer, iter_size);

    if (iter_size <= 0)
      break;

    res += iter_size;
    ptr += iter_size;
    size -= iter_size;
  }

  if (res == 0)
    res = -1;

  cable->access(true, (unsigned int)(long)&target_req->write.retval, 4, (char*)&res);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  int res = close(req->close.file);
  cable->access(true, (unsigned int)(long)&target_req->write.retval, 4, (char*)&res);
  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  switch (req->type)
  {
    case HAL_BRIDGE_REQ_CONNECT:    return this->handle_req_connect(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_DISCONNECT: return this->handle_req_disconnect(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_OPEN:       return this->handle_req_open(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_READ:       return this->handle_req_read(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_WRITE:      return this->handle_req_write(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_CLOSE:      return this->handle_req_close(debug_struct, req, target_req);
    default:
      this->log->print(LOG_ERROR, "Received unknown request from target (type: %d)\n", req->type);
  }
  return false;
}


void Reqloop::reqloop_routine()
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

      // Check printf
      // The target application should quickly dumps the characters, so we can loop on printf
      // until we don't find anything
      while(1) {
        hal_bridge_req_t *first_bridge_req, *last_req, *next, *next_next;

        cable->access(false, (unsigned int)(long)&debug_struct->first_bridge_req, 4, (char*)&first_bridge_req);

        if (first_bridge_req == NULL)
          break;

        hal_bridge_req_t req;
        this->cable->access(false, (unsigned int)(long)first_bridge_req, sizeof(hal_bridge_req_t), (char*)&req);

        value = 1;
        cable->access(true, (unsigned int)(long)&first_bridge_req->popped, sizeof(first_bridge_req->popped), (char*)&value);
        cable->access(true, (unsigned int)(long)&debug_struct->first_bridge_req, 4, (char*)&req.next);

        if (this->handle_req(debug_struct, &req, first_bridge_req))
          return;
      }

      // Small sleep to not poll too often
      usleep(50000);
    }

  }
}

Reqloop::Reqloop(Cable *cable, unsigned int debug_struct_addr) : cable(cable), debug_struct_addr(debug_struct_addr)
{
  log = new Log();
  activate();
  thread = new std::thread(&Reqloop::reqloop_routine, this);
}

extern "C" void *bridge_reqloop_open(void *cable, unsigned int debug_struct_addr)
{
  return (void *)new Reqloop((Cable *)cable, debug_struct_addr);
}

extern "C" void bridge_reqloop_close(void *arg, int kill)
{
  Reqloop *reqloop = (Reqloop *)arg;
  reqloop->stop(kill);
}


