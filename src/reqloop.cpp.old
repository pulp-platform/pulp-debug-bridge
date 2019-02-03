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
#include <string>
#include <algorithm>

#include "loops.hpp"

#if defined(__USE_SDL__)
void Framebuffer::Window::destroy()
{
  if (m_destroyed) return;
  SDL_DestroyTexture(m_texture);
  SDL_DestroyRenderer(m_renderer);
  SDL_DestroyWindow(m_window);
}

void Framebuffer::Window::handle_display()
{
  m_pixels = new uint32_t[m_width*m_height];
  m_window = SDL_CreateWindow(m_name.c_str(),
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, m_width, m_height, 0);

  m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);

  m_texture = SDL_CreateTexture(m_renderer,
      SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, m_width, m_height);

  memset(m_pixels, 255, m_width * m_height * sizeof(Uint32));

  SDL_UpdateTexture(m_texture, NULL, m_pixels, m_width * sizeof(Uint32));
  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, NULL, NULL);
  SDL_RenderPresent(m_renderer);
  m_destroyed = false;
}

void Framebuffer::Window::handle_update(Framebuffer::UpdateMessage * msg)
{
  if (m_destroyed) return;
  for (int j=0; j<msg->m_height; j++)
  {
    for (int i=0; i<msg->m_width; i++)
    {
      unsigned int value = msg->m_buffer[j*msg->m_width+i];
      m_pixels[(j+msg->m_posy)*m_width + i + msg->m_posx] = (0xff << 24) | (value << 16) | (value << 8) | value;
    }
  }

  SDL_UpdateTexture(m_texture, NULL, m_pixels, m_width * sizeof(Uint32));
  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, NULL, NULL);
  SDL_RenderPresent(m_renderer);
}

Framebuffer::Framebuffer(Reqloop *reqloop)
: m_top(reqloop)
{
}

void Framebuffer::start()
{
  m_thread = new std::thread(&Framebuffer::fb_routine, this);
}

void Framebuffer::destroy()
{
  if (m_destroyed) return;
  m_destroyed = true;
  m_running = false;
  if (m_thread->joinable())
    m_thread->join();
  m_thread = NULL;
}

void Framebuffer::fb_routine()
{
  SDL_Init(SDL_INIT_VIDEO);
  m_display_window = SDL_RegisterEvents(1);
  m_update_window = SDL_RegisterEvents(1);
  m_running = true;
  emit_start();
  SDL_Event event;

  while (m_running)
  {
    SDL_WaitEventTimeout(&event, 250);
    if (!m_running) break;
    if (event.type == SDL_QUIT) {
      m_running = false;
      break;
    } else if (event.type == SDL_WINDOWEVENT) {
      if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
        m_running = false;
        break;
      }
    } else if (event.type == m_display_window) {
      handle_display(event.user.code);
    } else if (event.type == m_update_window) {
      handle_update(event.user.code, event.user.data1);
    }
  }
  {
    std::lock_guard<std::mutex> guard(m_mut_windows);
    m_windows.clear();
  }
  SDL_Quit();
 }

void Framebuffer::open(std::string name, int width, int height, int format)
{
  uint32_t window_id;
  if (format == HAL_BRIDGE_REQ_FB_FORMAT_GRAY)
  {

  }
  else
  {
    m_top->m_top->log.error("reqloop framebuffer - unsupported format: %d\n", format);
  }
  {
    std::lock_guard<std::mutex> guard(m_mut_windows);
    window_id = m_next_window_id++;
    m_windows[window_id] = std::make_shared<Framebuffer::Window>(name, width, height, format);
  }
  SDL_Event ev;
  SDL_memset(&ev, 0, sizeof(ev));
  ev.type = m_display_window;
  ev.user.code = window_id;
  SDL_PushEvent(&ev);
}

std::shared_ptr<Framebuffer::Window> Framebuffer::find_window(uint32_t window_id)
{
  std::lock_guard<std::mutex> guard(m_mut_windows);
  auto iwindow = m_windows.find(window_id);
  if (iwindow == m_windows.end()) {
    m_top->m_top->log.error("window id %x not found.\n", window_id);
    return nullptr;
  }
  return iwindow->second;
}

void Framebuffer::handle_display(uint32_t window_id)
{
  auto window = find_window(window_id);
  if (window) {
    window->handle_display();
    emit_window_open(window_id);
  }
}

void Framebuffer::handle_update(uint32_t window_id, void * vmsg)
{
  auto window = find_window(window_id);
  auto msg = reinterpret_cast<Framebuffer::UpdateMessage *>(vmsg);
  if (window) {
    window->handle_update(msg);
  }
  delete msg;
}

bool Framebuffer::update(uint32_t window_id, uint32_t addr, int posx, int posy, int width, int height)
{
  auto window = find_window(window_id);
  if (!window) return false;

  if (posx == -1)
  {
    posx = posy = 0;
    width = window->get_width();
    height = window->get_height();
  }
  auto msg = new Framebuffer::UpdateMessage(posx, posy, width, height, window->get_pixel_size());

  m_top->m_top->access(false, addr, msg->get_buffer_size(), static_cast<char *>(msg->get_buffer()));
  SDL_Event ev;
  SDL_memset(&ev, 0, sizeof(ev));
  ev.type = m_update_window;
  ev.user.code = window_id;
  ev.user.data1 = reinterpret_cast<void *>(msg);
  SDL_PushEvent(&ev);
  return true;
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
  m_top->log.detail("reqloop connect\n");
  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  m_top->log.detail("reqloop disconnect\n");
  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedStop;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  std::vector<char> name(req->open.name_len+1);

  m_top->access(false, (unsigned int)req->open.name, req->open.name_len+1, &(name[0]));
  int res = open(&(name[0]), req->open.flags, req->open.mode);

  m_top->log.detail("reqloop open %s fd %d\n", &(name[0]), res);

  m_top->access(true, PTR_2_INT(&target_req->open.retval), 4, (char*)&res);

  reply_req(debug_struct, target_req, req);

  return ReqloopFinishedMoreReqs;
}


Reqloop::FileReqState::FileReqState(uint32_t fd, uint32_t size, uint32_t ptr) :
  m_fd(fd), m_size(size), m_ptr(ptr) {
  m_iter_size = m_size;
  m_res = 0;
  if (m_iter_size > 4096)
    m_iter_size = 4096;
}

bool Reqloop::FileReqState::do_write(LoopManager * top)
{
  char buffer[4096];
  int len = std::min(m_size, m_iter_size);

  top->access(false, m_ptr, len, (char*)buffer);

  len = write(m_fd, (void *)buffer, len);

  if (len <= 0) {
    m_res = -1;
    return false;
  }

  m_res += len;
  m_ptr += len;
  m_size -= len;
  return m_size>0;
}

bool Reqloop::FileReqState::do_read(LoopManager * top)
{
  char buffer[4096];
  int len = std::min(m_size, m_iter_size);

  len = read(m_fd, (void *)buffer, len);

  if (len <= 0) {
    if (len == -1 && m_res == 0) m_res = -1;
    return false;
  }

  top->access(true, m_ptr, len, (char*)buffer);

  m_res += len;
  m_ptr += len;
  m_size -= len;
  return m_size>0;
}

int64_t Reqloop::handle_req_read_one(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  if (m_destroyed) return kEventLoopTimerDone;
  try {
    if (m_fstate.do_read(m_top)) {
      return m_req_pause;
    } else {
      this->m_top->access(true, PTR_2_INT(&target_req->read.retval), 4, (char*)&m_fstate.m_res);
      reply_req(debug_struct, target_req, req);
      set_paused(false);
      return kEventLoopTimerDone;
    }
  } catch (LoopCableException e) {
    log.error("Reqloop loop cable error: exiting\n");
    return kEventLoopTimerDone;
  }
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  m_fstate = FileReqState(req->read.file, req->read.len, req->read.ptr);
  m_top->log.detail("reqloop read fd %u %u\n", m_fstate.m_fd, m_fstate.m_size);
  if (handle_req_read_one(debug_struct, req, target_req) == m_req_pause) {
    m_active_timer = m_event_loop->getTimerEvent(std::bind(&Reqloop::handle_req_read_one, this->shared_from_this(), debug_struct, req, target_req), m_req_pause);
    return ReqloopFinishedCompletingReq;
  }
  return ReqloopFinishedMoreReqs;
}

int64_t Reqloop::handle_req_write_one(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  if (m_destroyed) return kEventLoopTimerDone;
  try {
    if (m_fstate.do_write(m_top)) {
      return m_req_pause;
    } else {
      this->m_top->access(true, PTR_2_INT(&target_req->write.retval), 4, (char*)&m_fstate.m_res);
      reply_req(debug_struct, target_req, req);
      set_paused(false);
      return kEventLoopTimerDone;
    }
  } catch (LoopCableException e) {
    log.error("Reqloop loop cable error: exiting\n");
    return kEventLoopTimerDone;
  }
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  m_fstate = FileReqState(req->write.file, req->write.len, req->write.ptr);
  m_top->log.detail("reqloop write fd %u %u\n", m_fstate.m_fd, m_fstate.m_size);
  if (handle_req_write_one(debug_struct, req, target_req) == m_req_pause) {
    m_active_timer = m_event_loop->getTimerEvent(std::bind(&Reqloop::handle_req_write_one, this->shared_from_this(), debug_struct, req, target_req), m_req_pause);
    return ReqloopFinishedCompletingReq;
  }
  return ReqloopFinishedMoreReqs;
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  int res = close(req->close.file);
  m_top->log.detail("reqloop close fd %x\n", req->close.file);
  m_top->access(true, PTR_2_INT(&target_req->write.retval), 4, (char*)&res);
  reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
}

#if defined(__USE_SDL__)
// The window open function is split into 2 stages. Firstly intialize the framebuffer thread. Then open the window.
// All calls are marshalled onto the framebuffer thread and then marshalled back onto the main loop
Reqloop::ReqloopFinishedStatus Reqloop::handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  if (!m_frame_buffer) {
    m_top->log.detail("reqloop start framebuffer\n");
    m_frame_buffer = new Framebuffer(this);
    // get an async event that will be triggered on the SDL thread
    // need a copy of the request since the event will execute after this function returns
    hal_bridge_req_t * req_copy = new hal_bridge_req_t(*req);
    auto sp_this = this->shared_from_this();
    auto ae = m_event_loop->getAsyncEvent<bool>([sp_this, debug_struct, req_copy, target_req](bool UNUSED(b)){
      // Move to the next step - executed on main loop
      if (sp_this->m_destroyed) return;
      sp_this->complete_req_fb_open(debug_struct, req_copy, target_req);
      delete req_copy;
    });
    m_frame_buffer->on_start([this, ae](){
      // executed on SDL thread
      m_top->log.detail("reqloop trigger start framebuffer finished\n");
      ae->trigger(true);
    });
    m_frame_buffer->start();
  } else {
    complete_req_fb_open(debug_struct, req, target_req);
  }
  return ReqloopFinishedCompletingReq;
}

void Reqloop::complete_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  if (m_destroyed) return;
  char name[req->fb_open.name_len+1];
  m_top->access(false, req->fb_open.name, req->fb_open.name_len+1, (char*)name);

  m_top->log.detail("reqloop open window %s\n", &(name[0]));
  // don't get destroyed until the event loop is or the callback is executed
  auto sp_this = this->shared_from_this();
  auto ae = m_event_loop->getAsyncEvent<uint32_t>([sp_this, debug_struct, req, target_req](uint32_t window_id){
    if (sp_this->m_destroyed) return;
    sp_this->complete_req_fb_window_open(debug_struct, req, target_req, window_id);
  });
  m_frame_buffer->once_window_open([ae](uint32_t window_id){
    ae->trigger(window_id);
  });
  // req is valid up to this point
  m_frame_buffer->open(&name[0], req->fb_open.width, req->fb_open.height, req->fb_open.format);
}

void Reqloop::complete_req_fb_window_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req, uint32_t window_id)
{
  m_top->access(true, PTR_2_INT(&target_req->fb_open.screen), 8, (char*)&window_id);
  // BEWARE! req is actually invalid here since it was either deleted or went out of scope however reply_req doesn't use it.
  reply_req(debug_struct, target_req, req);
  set_paused(false);
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  if  (!m_frame_buffer)
    return ReqloopFinishedStop;
  if (!m_frame_buffer->update((uint32_t) (req->fb_update.screen&0xffffffff), req->fb_update.addr, 
          req->fb_update.posx, req->fb_update.posy, req->fb_update.width, req->fb_update.height)
      )
  {
    m_frame_buffer->destroy();
    delete m_frame_buffer;
    return ReqloopFinishedStopAll;
  }
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

#ifdef __NEW_REQLOOP__
Reqloop::ReqloopFinishedStatus Reqloop::handle_req_target_status_sync(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req)
{
  hal_target_state_t target;

  m_top->access(false, PTR_2_INT(&debug_struct->target), sizeof(hal_target_state_t), (char *)&target);
  m_top->target_state_sync(&target);
  log.detail("New target state %d\n", target);

  this->reply_req(debug_struct, target_req, req);
  return ReqloopFinishedMoreReqs;
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
#ifdef __NEW_REQLOOP__
    case HAL_BRIDGE_REQ_TARGET_STATUS_SYNC:
                                    return handle_req_target_status_sync(debug_struct, req, target_req);
#endif
    default:
      log.print(LOG_ERROR, "Received unknown request from target (type: %d)\n", req->type);
  }
  return ReqloopFinishedStop;
}

LooperFinishedStatus Reqloop::register_proc(hal_debug_struct_t *debug_struct) {
  try {
    // notify that we are connected
    uint32_t connected = 1;
#ifdef __NEW_REQLOOP__
    m_top->access(true, PTR_2_INT(&debug_struct->bridge.connected), 4, (char*)&connected);
#else
    m_top->access(true, PTR_2_INT(&debug_struct->bridge_connected), 4, (char*)&connected);
#endif
    return LooperFinishedContinue;
  } catch (LoopCableException e) {
    log.error("Reqloop loop cable error: exiting\n");
    return LooperFinishedStopAll;
  }
}

Reqloop::ReqloopFinishedStatus Reqloop::handle_one_req(hal_debug_struct_t *debug_struct) {
  try {
#if defined(__NEW_REQLOOP__) && defined(__CHECK_AVAILABILITY__)
    if (!m_top->get_target_available()) return ReqloopFinishedContinue;
#endif
    uint32_t value;
    hal_bridge_req_t *first_bridge_req = NULL;

    m_top->access(false, PTR_2_INT(&debug_struct->first_bridge_req), 4, (char*)&first_bridge_req);

    if (first_bridge_req == NULL) return ReqloopFinishedContinue;

    hal_bridge_req_t req;
    m_top->access(false, PTR_2_INT(first_bridge_req), sizeof(hal_bridge_req_t), (char*)&req);

    value = 1;
    m_top->access(true, PTR_2_INT(&first_bridge_req->popped), sizeof(first_bridge_req->popped), (char*)&value);
    m_top->access(true, PTR_2_INT(&debug_struct->first_bridge_req), 4, (char*)&req.next);

    auto status = handle_req(debug_struct, &req, first_bridge_req);
    return status;
  } catch (LoopCableException e) {
    return ReqloopFinishedStopAll;
  }
}

LooperFinishedStatus Reqloop::loop_proc(hal_debug_struct_t *debug_struct)
{
  while (1) {
    if (m_has_error) return LooperFinishedStop;

    ReqloopFinishedStatus status = handle_one_req(debug_struct);
    switch(status) {
      case ReqloopFinishedCompletingReq:
        return LooperFinishedPause;
      case ReqloopFinishedContinue:
        return LooperFinishedContinue;
      case ReqloopFinishedMoreReqs:
        continue;
      case ReqloopFinishedStop:
        if (m_active_timer)
          m_active_timer->setTimeout(-1);
        return LooperFinishedStop;
      default:
        if (m_active_timer)
          m_active_timer->setTimeout(-1);
        return LooperFinishedStopAll;
    }
  }
}

Reqloop::Reqloop(LoopManager * top, const EventLoop::SpEventLoop &event_loop, int64_t req_pause) : Looper(top), log("REQLOOP"), m_event_loop(event_loop), m_req_pause(req_pause)
{
}

