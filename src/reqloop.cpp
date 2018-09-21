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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__USE_SDL__)
#include <SDL.h>
#endif

#include "loops.hpp"

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
    width = width;
    height = height;
  }

  int size = width*height*pixel_size;
  uint8_t buffer[size];
  m_top->access(false, addr, size, (char*)buffer);

  for (int j=0; j<height; j++)
  {
    for (int i=0; i<width; i++)
    {
      unsigned int value = buffer[j*width+i];
      pixels[(j+posy)*width + i + posx] = (0xff << 24) | (value << 16) | (value << 8) | value;
    }
  }

  SDL_UpdateTexture(texture, NULL, pixels, width * sizeof(Uint32));

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}
#endif

void Reqloop::reply_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *)
{
  uint32_t value = 1;
  m_top->access(true, PTR_2_INT(&target_req->done), sizeof(target_req->done), (char*)&value);

  uint32_t notif_req_addr;
  uint32_t notif_req_value;
  m_top->access(false, PTR_2_INT(&debug_struct->notif_req_addr), 4, (char*)&notif_req_addr);
  m_top->access(false, PTR_2_INT(&debug_struct->notif_req_value), 4, (char*)&notif_req_value);

  m_top->access(true, notif_req_addr, 4, (char*)&notif_req_value);
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

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_connect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedStop;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  std::vector<char> name(req->open.name_len+1);

  m_top->access(false, (unsigned int)req->open.name, req->open.name_len+1, &(name[0]));

  int res = open(&(name[0]), req->open.flags, req->open.mode);

  m_top->access(true, PTR_2_INT(&target_req->open.retval), 4, (char*)&res);

  reply_req(debug_struct, target_req, req);

  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
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

    m_top->access(true, PTR_2_INT(ptr), iter_size, (char*)buffer);

    res += iter_size;
    ptr += iter_size;
    size -= iter_size;
  }

  m_top->access(true, PTR_2_INT(&target_req->read.retval), 4, (char*)&res);

  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
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

    m_top->access(false, PTR_2_INT(ptr), iter_size, (char*)buffer);

    iter_size = write(req->write.file, (void *)buffer, iter_size);

    if (iter_size <= 0)
      break;

    res += iter_size;
    ptr += iter_size;
    size -= iter_size;
  }

  if (res == 0)
    res = -1;

  m_top->access(true, PTR_2_INT(&target_req->write.retval), 4, (char*)&res);

  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  int res = close(req->close.file);
  m_top->access(true, PTR_2_INT(&target_req->write.retval), 4, (char*)&res);
  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

#if defined(__USE_SDL__)
Reqloop::ReqloopFinishedStatus Reqloop::handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  char name[req->fb_open.name_len+1];
  m_top->access(false, PTR_2_INT(req->fb_open.name), req->fb_open.name_len+1, (char*)name);

  Framebuffer *fb = new Framebuffer(cable, name, req->fb_open.width, req->fb_open.height, req->fb_open.format);

  if (!fb->open()) 
  {
    delete fb;
    fb = NULL;
  }

  m_top->access(true, PTR_2_INT(&target_req->fb_open.screen), 8, (char*)&fb);

  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  Framebuffer *fb = (Framebuffer *)req->fb_update.screen;

  fb->update(
    req->fb_update.addr, req->fb_update.posx, req->fb_update.posy, req->fb_update.width, req->fb_update.height
  );

  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}
#else
Reqloop::ReqloopFinishedStatus Reqloop::handle_req_fb_update(hal_debug_struct_t *, hal_bridge_req_t *, hal_bridge_req_t *)
{
  log.error("attempt to update framebuffer but bridge is not compiled with SDL");
  return ReqloopFinishedStop;
}
Reqloop::ReqloopFinishedStatus Reqloop::handle_req_fb_open(hal_debug_struct_t *, hal_bridge_req_t *, hal_bridge_req_t *)
{
  log.error("attempt to open framebuffer but bridge is not compiled with SDL");
  return ReqloopFinishedStop;
}
#endif

Reqloop::ReqloopFinishedStatus Reqloop::handle_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  switch (req->type)
  {
    case HAL_BRIDGE_REQ_CONNECT:    return handle_req_connect(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_DISCONNECT: return handle_req_disconnect(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_OPEN:       return handle_req_open(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_READ:       return handle_req_read(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_WRITE:      return handle_req_write(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_CLOSE:      return handle_req_close(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_FB_OPEN:    return handle_req_fb_open(debug_struct, req, target_req);
    case HAL_BRIDGE_REQ_FB_UPDATE:  return handle_req_fb_update(debug_struct, req, target_req);
    default:
      log.print(LOG_ERROR, "Received unknown request from target (type: %d)\n", req->type);
  }
  return ReqloopFinishedStop;
}

LooperFinishedStatus Reqloop::register_proc(hal_debug_struct_t *debug_struct) {
  try {
    // notify that we are connected
    uint32_t connected = 1;
    m_top->access(true, PTR_2_INT(&debug_struct->bridge_connected), 4, (char*)&connected);
    return LooperFinishedContinue;
  } catch (LoopCableException e) {
    log.error("IO loop cable error: exiting\n");
    return LooperFinishedStopAll;
  }
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_one_req(hal_debug_struct_t *debug_struct) {
  try {
    uint32_t value;
    hal_bridge_req_t *first_bridge_req = NULL;

    m_top->access(false, PTR_2_INT(&debug_struct->first_bridge_req), 4, (char*)&first_bridge_req);

    if (first_bridge_req == NULL) return ReqloopFinishedContinue;

    hal_bridge_req_t req;
    m_top->access(false, PTR_2_INT(first_bridge_req), sizeof(hal_bridge_req_t), (char*)&req);

    value = 1;
    m_top->access(true, PTR_2_INT(&first_bridge_req->popped), sizeof(first_bridge_req->popped), (char*)&value);
    m_top->access(true, PTR_2_INT(&debug_struct->first_bridge_req), 4, (char*)&req.next);

    return handle_req(debug_struct, &req, first_bridge_req);
  } catch (LoopCableException e) {
    return ReqloopFinishedStopAll;
  }
}

void Reqloop::setup_request_timer(hal_debug_struct_t *debug_struct) {
    m_event_loop->getTimerEvent([this, debug_struct](){
      ReqloopFinishedStatus status = handle_one_req(debug_struct);
      switch(status) {
        case ReqloopFinishedCompletingReq:
          return kEventLoopTimerDone;
        case ReqloopFinishedContinue:
          set_paused(false);
          return kEventLoopTimerDone;
        case ReqloopFinishedMoreReqs:
          return m_req_pause;
        case ReqloopFinishedStop:
          m_top->remove_looper(this);
          return kEventLoopTimerDone;
        default:
          m_top->clear_loopers();
          return kEventLoopTimerDone;
      }
    }, 0);
}

LooperFinishedStatus Reqloop::loop_proc(hal_debug_struct_t *debug_struct)
{
  if (m_has_error) return LooperFinishedStop;

  ReqloopFinishedStatus status = handle_one_req(debug_struct);
  switch(status) {
    case ReqloopFinishedCompletingReq:
      return LooperFinishedPause;
    case ReqloopFinishedContinue:
      return LooperFinishedContinue;
    case ReqloopFinishedMoreReqs:
      setup_request_timer(debug_struct);
      return LooperFinishedPause;
    case ReqloopFinishedStop:
      return LooperFinishedStop;
    default:
      return LooperFinishedStopAll;
  }
}

Reqloop::Reqloop(LoopManager * top, const EventLoop::SpEventLoop &event_loop, int64_t req_pause) : Looper(top), log("REQLOOP"), m_event_loop(event_loop), m_req_pause(req_pause)
{
}


