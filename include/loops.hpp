/*
 * Copyright (C) 2018 ETH Zurich, University of Bologna and GreenWaves Technologies SA
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
 * Authors: Martin Croome, GWT (martin.croome@greenwaves.technologies.com)
 */

#ifndef __LOOPS_H
#define __LOOPS_H

#include <stdint.h>
#include <list>
#include <queue>
#include <memory>
#include <functional>
#include <exception>

#include "debug_bridge/debug_bridge.h"
#include "cables/cable.hpp"
#include "events/events.hpp"

#define PTR_2_INT(__addr) ((unsigned int)(reinterpret_cast<std::uintptr_t>(__addr)&0xffffffff))
#define INT_2_PTR(__addr) (reinterpret_cast<std::uintptr_t>((size_t)__addr))

#define LOOP_DEFAULT_LOOP_USECS 500
#define LOOP_DEFAULT_SLOW_LOOP_USECS 10000000

class LoopCableException: public std::exception
{
public:
  const char* what() const throw()
  {
    return "Exception accessing cable";
  }
};

enum LooperFinishedStatus {
  LooperFinishedContinue,
  LooperFinishedPause,
  LooperFinishedStop,
  LooperFinishedStopAll
};

class LoopManager;

class Looper {
public:
  Looper(LoopManager * top) : m_top(top) {}
  virtual ~Looper() {}
  virtual LooperFinishedStatus loop_proc(hal_debug_struct_t * debug_struct) = 0;
  virtual LooperFinishedStatus register_proc(hal_debug_struct_t * debug_struct) = 0;
  bool get_paused() { return m_paused; }
  void set_paused(bool paused) { m_paused = paused; }
protected:
  LoopManager * m_top;
  bool m_paused = false;
};

class LoopManager {
public:
  LoopManager(const EventLoop::SpEventLoop &event_loop, std::shared_ptr<Cable> cable, unsigned int debug_struct_addr, 
      int64_t slow_usecs = LOOP_DEFAULT_SLOW_LOOP_USECS, int64_t fast_usecs = LOOP_DEFAULT_LOOP_USECS);
  ~LoopManager();
  void set_debug_struct_addr(unsigned int debug_struct_addr);
  void start(bool fast);
  void set_loop_speed(bool fast);
  void stop();
  void add_looper(const std::shared_ptr<Looper> &looper);
  void remove_looper(Looper * looper);
  void clear_loopers() {
    stop();
    m_loopers.clear();
  }
  void access(bool write, unsigned int addr, int len, char * buf);
  int64_t run_loops();
private:
  hal_debug_struct_t * activate();

  Log log;
  EventLoop::SpTimerEvent m_loop_te;
  std::list<std::shared_ptr<Looper>> m_loopers;

  std::shared_ptr<Cable> m_cable;     
  unsigned int m_debug_struct_addr;
  int64_t m_slow_usecs, m_fast_usecs, m_cur_usecs;
  bool m_stopped = true;
};

class Reqloop : public Looper
{
public:
  Reqloop(LoopManager * top, const EventLoop::SpEventLoop &event_loop, int64_t req_pause = 0);

  LooperFinishedStatus loop_proc(hal_debug_struct_t *debug_struct);
  LooperFinishedStatus register_proc(hal_debug_struct_t *debug_struct);
private:
  enum ReqloopFinishedStatus {
    ReqloopFinishedContinue,
    ReqloopFinishedMoreReqs,
    ReqloopFinishedCompletingReq,
    ReqloopFinishedStop,
    ReqloopFinishedStopAll
  };
  void reply_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req_connect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req_read(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req_write(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req_close(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req_fb_open(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  ReqloopFinishedStatus handle_req_fb_update(hal_debug_struct_t *debug_struct, hal_bridge_req_t *req, hal_bridge_req_t *target_req);
  ReqloopFinishedStatus handle_req_disconnect(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_req(hal_debug_struct_t *debug_struct, hal_bridge_req_t *target_req, hal_bridge_req_t *req);
  ReqloopFinishedStatus handle_one_req(hal_debug_struct_t *debug_struct);
  void setup_request_timer(hal_debug_struct_t *debug_struct);

  Log log;
  EventLoop::SpEventLoop m_event_loop;
  bool m_has_error = false;
  int64_t m_req_pause;
};

class Ioloop : public Looper
{
public:
  using ProgramExitFunction = std::function<void(int)>;
  Ioloop(LoopManager * top, const EventLoop::SpEventLoop &event_loop, int64_t printing_pause = 0);
  ~Ioloop() {}
  LooperFinishedStatus loop_proc(hal_debug_struct_t *debug_struct);
  LooperFinishedStatus register_proc(hal_debug_struct_t *debug_struct);
  void on_exit(const ProgramExitFunction &exit_func);
private:
  uint32_t print_len(hal_debug_struct_t *debug_struct);
  void print_one(hal_debug_struct_t *debug_struct, uint32_t len);
  void print_loop(hal_debug_struct_t *debug_struct);

  Log log;
  EventLoop::SpEventLoop m_event_loop;
  std::queue<ProgramExitFunction> m_exit_queue;
  int64_t m_printing_pause;
};
#endif
