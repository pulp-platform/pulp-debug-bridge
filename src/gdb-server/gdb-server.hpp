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

#ifndef __GDB_SERVER_GDB_SERVER_H__
#define __GDB_SERVER_GDB_SERVER_H__

#include <list>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>



#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <thread>
#include <unordered_map>

#include "cable.hpp"
#include "json.hpp"

#include "Tcp_listener.hpp"

#define DBG_CTRL_REG  0x0
#define DBG_HIT_REG   0x4
#define DBG_IE_REG    0x8
#define DBG_CAUSE_REG 0xC
#define DBG_NPC_REG   0x2000
#define DBG_PPC_REG   0x2004

#define DBG_CAUSE_BP  0x3

enum exception_causes {
  EXC_CAUSE_NONE         = 0x00,
  EXC_CAUSE_ILLEGAL_NONE = 0x02,
  EXC_CAUSE_ILLEGAL_INSN = 0x02,
  EXC_CAUSE_BREAKPOINT   = 0x03,
  EXC_CAUSE_ECALL_UMODE  = 0x08,
  EXC_CAUSE_ECALL_MMODE  = 0x0B,
  EXC_CAUSE_DBG_HALT     = 0x1F,
  EXC_CAUSE_MASK         = EXC_CAUSE_DBG_HALT
};

#define EXC_CAUSE_INTERUPT(cause) (cause&(1<<31))

enum target_signal {
  TARGET_SIGNAL_NONE =  0,
  TARGET_SIGNAL_INT  =  2,
  TARGET_SIGNAL_ILL  =  4,
  TARGET_SIGNAL_TRAP =  5,
  TARGET_SIGNAL_FPE  =  8,
  TARGET_SIGNAL_KILL  = 9,
  TARGET_SIGNAL_BUS  = 10,
  TARGET_SIGNAL_SEGV = 11,
  TARGET_SIGNAL_ALRM = 14,
  TARGET_SIGNAL_STOP = 17,
  TARGET_SIGNAL_USR2 = 31,
  TARGET_SIGNAL_PWR  = 32
};

typedef int (*cmd_cb_t)(const char * cmd, char * rsp_buf, int rsp_buf_len);

class Rsp;
class Breakpoints;
class Target;
class Target_cluster_common;
class Target_cluster;
class Target_core;

class Gdb_server
{
public:
  Gdb_server(Log *log, Cable *cable, js::config *config, int socket_port,
    cmd_cb_t cmd_cb, const char * capabilities);
  int stop(bool kill);
  void print(const char *format, ...);
  int target_is_started();
  void start_target();
  void stop_target();
  void refresh_target();
  void target_update_power();

  Rsp *rsp;
  Log *log;
  Cable *cable;
  Target *target;
  Breakpoints *bkp;
  cmd_cb_t cmd_cb;
  const char * capabilities;
  js::config *config;
};




class Target_core
{
public:
  Target_core(Gdb_server *top, uint32_t dbg_unit_addr, int cluster_id, int core_id);
  void set_power(bool is_on);
  bool read(uint32_t addr, uint32_t* rdata);
  bool write(uint32_t addr, uint32_t wdata);
  bool csr_read(unsigned int i, uint32_t *data);
  int get_thread_id() { return this->thread_id; }
  int get_cluster_id() { return this->cluster_id; }
  int get_core_id() { return this->core_id; }
  void get_name(char* str, size_t len) {
    snprintf(str, len, "Cluster %02d - Core %01d", this->cluster_id, this->core_id);
  }

  bool actual_pc_read(unsigned int* pc);
  bool is_stopped();
  bool read_hit(bool *ss_hit, bool *is_sleeping);
  bool is_stopped_on_trap() { return pc_is_cached && on_trap; }
  uint32_t get_cause();
  uint32_t check_stopped();

  int get_signal();

  bool stop();
  bool halt();
  void prepare_resume(bool step=false);
  void clear_resume() { resume_prepared = false; }
  void commit_resume();
  bool should_resume() { return resume_prepared; }
  void set_step_mode(bool new_step);
  void commit_step_mode();
  void resume();
  void flush();
  void init(bool is_on);

  bool gpr_read_all(uint32_t *data);
  bool gpr_read(unsigned int i, uint32_t *data);
  bool gpr_write(unsigned int i, uint32_t data);

private:
  Gdb_server *top;
  bool is_on = false;
  uint32_t dbg_unit_addr;
  uint32_t hartid;
  int cluster_id;
  int core_id;
  int thread_id;
  bool pc_is_cached = false;
  uint32_t pc_cached;
  bool stopped = false;
  bool step = false;
  bool commit_step = false;
  bool resume_prepared = false;
  bool on_trap = false;
  static int first_free_thread_id;
};

class Target {
public:
  Target(Gdb_server *top);
  ~Target();
  inline int get_nb_threads() { return cores.size(); }


  void halt();
  // void resume(bool step=false, int tid=-1);
  void clear_resume_all();
  void prepare_resume_all(bool step);
  void resume_all();
  bool wait();
  void flush();
  void reinitialize();
  void update_power();
  Target_core * check_stopped();

  std::vector<Target_core *> get_threads() { return cores; }
  Target_core *get_thread(int thread_id) { return cores_from_threadid[thread_id]; }
  Target_core *get_thread_from_id(int id) { return cores[id]; }


private:
  Gdb_server *top;
  std::vector<Target_cluster_common *> clusters;
  std::vector<Target_core *> cores;
  std::map<int, Target_core *> cores_from_threadid;
};



struct bp_insn {
  uint32_t addr;
  uint32_t insn_orig;
  bool is_compressed;
};



class Breakpoints {
  public:
    Breakpoints(Gdb_server *top);

    bool insert(unsigned int addr);
    bool remove(unsigned int addr);

    bool clear();

    bool at_addr(unsigned int addr);

    bool enable_all();
    bool disable_all();

    bool disable(unsigned int addr);
    bool enable(unsigned int addr);

  private:
    std::list<struct bp_insn> breakpoints;
    Gdb_server *top;
};

enum capability_support {
  CAPABILITY_NOT_SUPPORTED = 0,
  CAPABILITY_MAYBE_SUPPORTED = 1,
  CAPABILITY_IS_SUPPORTED = 2
};

using std::unique_ptr;
class Rsp_capability;
using Rsp_capabilities = std::unordered_map<std::string,unique_ptr<Rsp_capability>>;

class Rsp_capability
{
public:
  Rsp_capability(const char * name, capability_support support);
  Rsp_capability(const char * name, const char * value);
  static void parse(char * buf, size_t len, Rsp_capabilities *caps);
  bool is_supported() { return support == CAPABILITY_IS_SUPPORTED; }
private:
  capability_support support;
  std::string name, value;
};



class Rsp {
  public:
    Rsp(Gdb_server *top, int port);

    bool open();
    void close(bool await_one);
    void init();
    void wait_finished(int cnt);

    class Client
    {
      public:
        Client(Rsp *rsp, Tcp_listener::Tcp_socket *client);
        void stop();
        bool is_running() { return running; };
      private:
        bool remote_capability(const char * name) {
          Rsp_capabilities::const_iterator it = remote_caps.find (name);
          return it != remote_caps.end() && it->second.get()->is_supported();
        }
        int cause_to_signal(uint32_t cause, int * int_num = NULL);
        int get_signal(Target_core *core);
        bool decode(char* data, size_t len);
        bool get_packet(char* data, size_t* len);

        void client_routine();

        bool regs_send();
        bool signal(Target_core *core);

        bool signal() { return this->signal(NULL); };

        bool multithread(char* data, size_t len);

        bool v_packet(char* data, size_t len);

        bool query(char* data, size_t len);

        bool send(const char* data, size_t len);
        bool send_str(const char* data);

        bool cont(char* data, size_t len); // continue, reserved keyword, thus not used as function name
        bool resume(bool step);
        bool resume(int tid, bool step);
        bool wait();
        bool step(char* data, size_t len);

        // internal helper functions

        bool reg_read(char* data, size_t len);
        bool reg_write(char* data, size_t len);

        bool mem_read(char* data, size_t len);
        bool mem_write_ascii(char* data, size_t len);
        bool mem_write(char* data, size_t len);

        bool bp_insert(char* data, size_t len);
        bool bp_remove(char* data, size_t len);

        bool running = true;
        Rsp_capabilities remote_caps;
        Gdb_server *top;
        bool stopped;

        Rsp *rsp;
        Tcp_listener::Tcp_socket *client;

        std::thread *thread;

        int thread_sel;
    };

  private:
    void client_connected(Tcp_listener::Tcp_socket* client);
    void client_disconnected(Tcp_listener::Tcp_socket *client);
    void rsp_client_finished();
    Target_core *main_core = NULL;

    Tcp_listener *listener;
    Rsp::Client *client;

    Gdb_server *top;
    int m_thread_init;
    int port;
    std::mutex m_finished, m_rsp_client;
    std::condition_variable cv_finished, cv_rsp_client;
    int conn_cnt=0;
};

#endif