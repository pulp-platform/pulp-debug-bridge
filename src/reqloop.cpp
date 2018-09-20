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
#include "debug_bridge/debug_bridge.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__USE_SDL__)
#include <SDL.h>
#endif

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
  bool handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  bool handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  bool handle_req_target_status_sync(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  bool handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  void update_target_status(hal_debug_struct_t *debug_struct);
  void wait_target_available(hal_debug_struct_t *debug_struct);

  Log *log;
  std::thread *thread;
  Cable *cable;
  bool end = false;
  unsigned int debug_struct_addr;
  int status = 0;

  hal_target_state_t target;


};

class Framebuffer
{
public:
  Framebuffer(Cable *cable, std::string name, int width, int height, int format);
  void update(uint32_t addr, int posx, int posy, int width, int height);
  bool open();

private:
  void fb_routine();

  std::string name;
  int width;
  int height;
  int format;
  int pixel_size = 1;
  Cable *cable;
  std::thread *thread;
  uint32_t *pixels;
#if defined(__USE_SDL__)
  SDL_Surface *screen;
  SDL_Texture * texture;
  SDL_Renderer *renderer;
  SDL_Window *window;
#endif
};

Framebuffer::Framebuffer(Cable *cable, std::string name, int width, int height, int format)
: name(name), width(width), height(height), format(format), cable(cable)
{
}

void Framebuffer::fb_routine()
{
#if defined(__USE_SDL__)
  bool quit = false;
  SDL_Event event;

  while (!quit)
  {
    SDL_WaitEvent(&event);
    switch (event.type)
    {
      case SDL_QUIT:
      quit = true;
      break;
    }
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
#endif
}


bool Framebuffer::open()
{
#if defined(__USE_SDL__)

  if (format == HAL_BRIDGE_REQ_FB_FORMAT_GRAY)
  {

  }
  else
  {
    printf("Unsupported format: %d\n", format);
  }

  pixels = new uint32_t[width*height];
  SDL_Init(SDL_INIT_VIDEO);

  window = SDL_CreateWindow(name.c_str(),
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, 0);

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  texture = SDL_CreateTexture(renderer,
      SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);

  memset(pixels, 255, width * height * sizeof(Uint32));

  SDL_UpdateTexture(texture, NULL, pixels, width * sizeof(Uint32));

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);

  thread = new std::thread(&Framebuffer::fb_routine, this);
  return true;

#else
  printf("Trying to open framebuffer while bridge has not been compiled with SDL support\n");
  return false;
#endif
}

void Framebuffer::update(uint32_t addr, int posx, int posy, int width, int height)
{
#if defined(__USE_SDL__)

  if (posx == -1)
  {
    posx = posy = 0;
    width = this->width;
    height = this->height;
  }

  int size = width*height*pixel_size;
  uint8_t buffer[size];
  this->cable->access(false, addr, size, (char*)buffer);

  for (int j=0; j<height; j++)
  {
    for (int i=0; i<width; i++)
    {
      unsigned int value = buffer[j*width+i];
      pixels[(j+posy)*this->width + i + posx] = (0xff << 24) | (value << 16) | (value << 8) | value;
    }
  }

  SDL_UpdateTexture(texture, NULL, pixels, this->width * sizeof(Uint32));

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
#endif
}



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
    cable->access(true, (unsigned int)(long)&debug_struct->bridge.connected, 4, (char*)&connected);
    cable->access(true, (unsigned int)(long)&debug_struct->use_internal_printf, 4, (char*)&value);
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

bool Reqloop::handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char name[req->fb_open.name_len+1];
  cable->access(false, (unsigned int)(long)req->fb_open.name, req->fb_open.name_len+1, (char*)name);

  int res = 0;
  Framebuffer *fb = new Framebuffer(cable, name, req->fb_open.width, req->fb_open.height, req->fb_open.format);



  if (!fb->open()) 
  {
    res = -1;
    delete fb;
    fb = NULL;
  }

  cable->access(true, (unsigned int)(long)&target_req->fb_open.screen, 8, (char*)&fb);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

void Reqloop::update_target_status(hal_debug_struct_t *debug_struct)
{
  this->cable->access(false, (unsigned int)(long)&debug_struct->target, sizeof(this->target), (char *)&this->target);
}

bool Reqloop::handle_req_target_status_sync(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  this->update_target_status(debug_struct);
  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
#if defined(__USE_SDL__)
  Framebuffer *fb = (Framebuffer *)req->fb_update.screen;

  fb->update(
    req->fb_update.addr, req->fb_update.posx, req->fb_update.posy, req->fb_update.width, req->fb_update.height
  );
#endif

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
    case HAL_BRIDGE_REQ_FB_OPEN:    return this->handle_req_fb_open(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_FB_UPDATE:  return this->handle_req_fb_update(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_TARGET_STATUS_SYNC:    return this->handle_req_target_status_sync(debug_struct, req, target_req);
    default:
      this->log->print(LOG_ERROR, "Received unknown request from target (type: %d)\n", req->type);
  }
  return false;
}


void Reqloop::wait_target_available(hal_debug_struct_t *debug_struct)
{
  if (!this->target.available)
  {
    while(1)
    {
      unsigned int value;
      this->cable->jtag_get_reg(7, 4, &value, 0);
      if (value & 2) break;
      usleep(10);
    }
    this->update_target_status(debug_struct);
  }
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

      this->wait_target_available(debug_struct);

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
        for (int i=0; i<value; i++) putchar(buff[i]);
        fflush(NULL);
      }

      while(1) {
        hal_bridge_req_t *first_bridge_req=NULL, *last_req=NULL, *next=NULL, *next_next=NULL;

        if (!cable->access(false, (unsigned int)(long)&debug_struct->first_bridge_req, 4, (char*)&first_bridge_req)) goto end;

        if (first_bridge_req == NULL)
          break;

        hal_bridge_req_t req;
        if (!this->cable->access(false, (unsigned int)(long)first_bridge_req, sizeof(hal_bridge_req_t), (char*)&req)) goto end;

        value = 1;
        if (!cable->access(true, (unsigned int)(long)&first_bridge_req->popped, sizeof(first_bridge_req->popped), (char*)&value)) goto end;
        if (!cable->access(true, (unsigned int)(long)&debug_struct->first_bridge_req, 4, (char*)&req.next)) goto end;

        if (this->handle_req(debug_struct, &req, first_bridge_req))
          return;

        this->wait_target_available(debug_struct);
      }

      // Small sleep to not poll too often
      usleep(500);
    }
  }
  else
  {
    log->warning("Trying to launch request loop (command reqloop) while no binary is provided\n");
  }

end:
  log->warning("Got access error in reqloop\n");
}

Reqloop::Reqloop(Cable *cable, unsigned int debug_struct_addr) : cable(cable), debug_struct_addr(debug_struct_addr)
{
  log = new Log();
  activate();
  this->target.available = 1;
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


