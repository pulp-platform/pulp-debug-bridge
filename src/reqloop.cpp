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
#include "cable.hpp"
#include "cables/log.h"
#include "debug_bridge/debug_bridge.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#if defined(__USE_SDL__)
#include <SDL.h>
#endif

#define PTR_2_INT(__addr) ((unsigned int)(reinterpret_cast<std::uintptr_t>(__addr)&0xffffffff))
#define INT_2_PTR(__addr) (reinterpret_cast<std::uintptr_t>((size_t)__addr))

#define DEFAULT_LOOP_DELAY 50000
#define DEFAULT_SLOW_LOOP_DELAY 250000

class ReqloopCableException: public std::exception
{
public:
  const char* what() const throw()
  {
    return "Exception accessing cable";
  }
};

class Reqloop
{
public:
  Reqloop(Log *log, Cable *cable, unsigned int debug_struct_addr);
  void reqloop_routine();
  int stop(bool kill);
  hal_debug_struct_t *activate();
  void set_poll_delay(int delay);
private:
  void reply_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_connect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  bool handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  bool handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  bool handle_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  void access(bool write, unsigned int addr, int len, char * buf);

  Log *log;
  std::thread *thread = NULL;
  Cable *cable;
  std::mutex joined_mutex;
  bool joined = false;
  bool end = false;
  unsigned int debug_struct_addr;
  std::atomic<int> delay;
  bool cable_error = false;
  int status = 0;
};

#if defined(__USE_SDL__)
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
  SDL_Surface *screen;
  SDL_Texture * texture;
  SDL_Renderer *renderer;
  SDL_Window *window;
};

Framebuffer::Framebuffer(Cable *cable, std::string name, int width, int height, int format)
: name(name), width(width), height(height), format(format), cable(cable)
{
}

void Framebuffer::fb_routine()
{
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
}


bool Framebuffer::open()
{

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
}

void Framebuffer::update(uint32_t addr, int posx, int posy, int width, int height)
{

  if (posx == -1)
  {
    posx = posy = 0;
    width = this->width;
    height = this->height;
  }

  int size = width*height*pixel_size;
  uint8_t buffer[size];
  this->access(false, addr, size, (char*)buffer);

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
}
#endif


int Reqloop::stop(bool kill)
{
  log->debug("reqloop: stop (kill: %d)\n", kill);
  if (thread) {
    if (kill) end = true;
    {
      std::lock_guard<std::mutex> guard(joined_mutex);
      if (joined||!thread->joinable()) {
        log->debug("reqloop: stop (kill: %d) completed (already joined or not joinable)\n", kill);
        return status;
      }
      joined = true;
    }
    log->debug("reqloop: stop (kill: %d) joining thread\n", kill);
    thread->join();
    delete(thread);
    thread = NULL;
  }
  log->debug("reqloop: stop (kill: %d) completed\n", kill);
  return status;
}

void Reqloop::access(bool write, unsigned int addr, int len, char * buf)
{
  if (!cable->access(write, addr, len, buf))
    throw ReqloopCableException();
}

hal_debug_struct_t *Reqloop::activate()
{
  hal_debug_struct_t *debug_struct = NULL;

  access(false, debug_struct_addr, 4, (char*)&debug_struct);

  if (debug_struct != NULL) {
    // The binary has just started, we need to tell him we want to watch for requests
    uint32_t connected = 1;
    access(true, PTR_2_INT(&debug_struct->bridge_connected), 4, (char*)&connected);
  }

  return debug_struct;
}



void Reqloop::reply_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *)
{
  uint32_t value = 1;
  this->access(true, PTR_2_INT(&target_req->done), sizeof(target_req->done), (char*)&value);

  uint32_t notif_req_addr;
  uint32_t notif_req_value;
  access(false, PTR_2_INT(&debug_struct->notif_req_addr), 4, (char*)&notif_req_addr);
  access(false, PTR_2_INT(&debug_struct->notif_req_value), 4, (char*)&notif_req_value);

  access(true, notif_req_addr, 4, (char*)&notif_req_value);
}

#if 0
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
#endif

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
  std::vector<char> name(req->open.name_len+1);

  access(false, (unsigned int)req->open.name, req->open.name_len+1, &(name[0]));

  int res = open(&(name[0]), req->open.flags, req->open.mode);

  access(true, PTR_2_INT(&target_req->open.retval), 4, (char*)&res);

  this->reply_req(debug_struct, target_req, req);

  return false;
}

bool Reqloop::handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char buffer[4096];
  int size = req->read.len;
  char *ptr = (char *)INT_2_PTR(req->read.ptr);
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

    access(true, PTR_2_INT(ptr), iter_size, (char*)buffer);

    res += iter_size;
    ptr += iter_size;
    size -= iter_size;
  }

  access(true, PTR_2_INT(&target_req->read.retval), 4, (char*)&res);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char buffer[4096];
  int size = req->write.len;
  char *ptr = (char *)INT_2_PTR(req->write.ptr);
  int res = 0;
  while (size)
  {
    int iter_size = size;
    if (iter_size > 4096)
      iter_size = 4096;

    access(false, PTR_2_INT(ptr), iter_size, (char*)buffer);

    iter_size = write(req->write.file, (void *)buffer, iter_size);

    if (iter_size <= 0)
      break;

    res += iter_size;
    ptr += iter_size;
    size -= iter_size;
  }

  if (res == 0)
    res = -1;

  access(true, PTR_2_INT(&target_req->write.retval), 4, (char*)&res);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  int res = close(req->close.file);
  access(true, PTR_2_INT(&target_req->write.retval), 4, (char*)&res);
  this->reply_req(debug_struct, target_req, req);
  return false;
}

#if defined(__USE_SDL__)
bool Reqloop::handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char name[req->fb_open.name_len+1];
  access(false, PTR_2_INT(req->fb_open.name), req->fb_open.name_len+1, (char*)name);

  Framebuffer *fb = new Framebuffer(cable, name, req->fb_open.width, req->fb_open.height, req->fb_open.format);



  if (!fb->open()) 
  {
    delete fb;
    fb = NULL;
  }

  access(true, PTR_2_INT(&target_req->fb_open.screen), 8, (char*)&fb);

  this->reply_req(debug_struct, target_req, req);
  return false;
}

bool Reqloop::handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  Framebuffer *fb = (Framebuffer *)req->fb_update.screen;

  fb->update(
    req->fb_update.addr, req->fb_update.posx, req->fb_update.posy, req->fb_update.width, req->fb_update.height
  );

  this->reply_req(debug_struct, target_req, req);
  return false;
}
#else
bool Reqloop::handle_req_fb_update(hal_debug_struct_t *, hal_bridge_req_t *, hal_bridge_req_t *)
{
  log->error("attempt to update framebuffer but bridge is not compiled with SDL");
  return false;
}
bool Reqloop::handle_req_fb_open(hal_debug_struct_t *, hal_bridge_req_t *, hal_bridge_req_t *)
{
  log->error("attempt to open framebuffer but bridge is not compiled with SDL");
  return false;
}
#endif

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
    default:
      this->log->print(LOG_ERROR, "Received unknown request from target (type: %d)\n", req->type);
  }
  return false;
}


void Reqloop::reqloop_routine()
{
  if (debug_struct_addr) {
    try {
      // In case the debug struct pointer is found, iterate to receive IO requests
      // from runtime
      while(!end)
      {

        hal_debug_struct_t *debug_struct = NULL;

        uint32_t value;

        // debugStruct_ptr is used to synchronize with the runtime at the first start or 
        // when we switch from one binary to another
        // Each binary will initialize when it boots will wait until we set it to zero before stopping
        while(!end) {
          if ((debug_struct = activate()) != NULL) break;

          // We use a fast loop to miss as few printf as possible as the binary will
          // start without waiting for any acknolegment in case
          // no host loader is connected
          usleep(1);
        }

        // Check printf
        // The target application should quickly dumps the characters, so we can loop on printf
        // until we don't find anything
        while(!end) {
          hal_bridge_req_t *first_bridge_req = NULL;

          access(false, PTR_2_INT(&debug_struct->first_bridge_req), 4, (char*)&first_bridge_req);

          if (first_bridge_req == NULL)
            break;

          hal_bridge_req_t req;
          access(false, PTR_2_INT(first_bridge_req), sizeof(hal_bridge_req_t), (char*)&req);

          value = 1;
          access(true, PTR_2_INT(&first_bridge_req->popped), sizeof(first_bridge_req->popped), (char*)&value);
          access(true, PTR_2_INT(&debug_struct->first_bridge_req), 4, (char*)&req.next);

          if (this->handle_req(debug_struct, &req, first_bridge_req))
            return;
        }

        int total_delay = delay;
        while (!end && total_delay > 0){
          usleep(DEFAULT_LOOP_DELAY);
          total_delay -= DEFAULT_LOOP_DELAY;
        }
      }
    } catch (ReqloopCableException e) {
      log->error("Cable error in reqloop");
    }
  }
  else
  {
    log->warning("Trying to launch request loop (command reqloop) while no binary is provided\n");
  }
}

void Reqloop::set_poll_delay(int delay)
{
  log->debug("reqloop delay set to %d\n", delay);
  this->delay = delay;
}

Reqloop::Reqloop(Log* log, Cable *cable, unsigned int debug_struct_addr) : log(log), cable(cable), debug_struct_addr(debug_struct_addr), delay(DEFAULT_LOOP_DELAY)
{
  try {
    activate();
    thread = new std::thread(&Reqloop::reqloop_routine, this);
  } catch (ReqloopCableException e) {
    log->error("Cable error starting reqloop");
  }
}

extern "C" void *bridge_reqloop_open(void *cable, unsigned int debug_struct_addr)
{
  return (void *)new Reqloop(new Log(), (Cable *)cable, debug_struct_addr);
}

extern "C" void bridge_reqloop_close(void *arg, int kill)
{
  Reqloop *reqloop = (Reqloop *)arg;
  reqloop->stop(kill);
}

extern "C" void bridge_reqloop_set_poll_delay(void *arg, int high_rate)
{
  Reqloop *reqloop = (Reqloop *)arg;
  if (high_rate)
    reqloop->set_poll_delay(DEFAULT_LOOP_DELAY);
  else
    reqloop->set_poll_delay(DEFAULT_SLOW_LOOP_DELAY);

}
