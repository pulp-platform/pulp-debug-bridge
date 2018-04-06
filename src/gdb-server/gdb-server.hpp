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
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <thread>

#include "cable.hpp"
#include "json.hpp"

#define DBG_CTRL_REG  0x0
#define DBG_HIT_REG   0x4
#define DBG_IE_REG    0x8
#define DBG_CAUSE_REG 0xC
#define DBG_NPC_REG   0x2000
#define DBG_PPC_REG   0x2004

#define DBG_CAUSE_BP  0x3


class Rsp;
class Target;
class Target_cluster;
class Target_core;

class Gdb_server
{
public:
  Gdb_server(Log *log, Cable *cable, js::config *config, int socket_port);
  int stop(bool kill);
  void print(const char *format, ...);

  Rsp *rsp;
  Log *log;
  Cable *cable;
  Target *target;

  js::config *config;
};




class Target_core
{
public:
  Target_core(Gdb_server *top, uint32_t dbg_unit_addr);
  void set_power(bool is_on);
  bool read(uint32_t addr, uint32_t* rdata);
  bool write(uint32_t addr, uint32_t wdata);
  bool csr_read(unsigned int i, uint32_t *data);
  int get_thread_id() { return core_id; }
  void get_name(char* str, size_t len) {
    snprintf(str, len, "Core %08X", this->core_id);
  }
  bool is_stopped() { return false; }
  void read_ppc(uint32_t *ppc);

  bool stop();
  bool halt();
  void prepare_resume(bool step=false);
  void commit_resume();
  void set_step_mode(bool new_step);
  void commit_step_mode();
  void resume();

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
  bool ppc_is_cached = false;
  uint32_t ppc_cached;
  bool stopped = false;
  bool step = false;
  bool commit_step = false;
};



class Target {
public:
  Target(Gdb_server *top);

  inline int get_nb_threads() { return nb_threads; }


  void halt();
  void resume(bool step=false);
  void resume_all();
  bool wait(int socket_client);

  void update_power();

  std::vector<Target_core *> get_threads() { return cores; }
  Target_core *get_thread(int thread_id) { return cores_from_threadid[thread_id]; }


private:
  Gdb_server *top;
  int nb_threads;
  std::vector<Target_cluster *> clusters;
  std::vector<Target_core *> cores;
  std::map<int, Target_core *> cores_from_threadid;
};


class Rsp {
  public:
    Rsp(Gdb_server *top, int socket_port);
    //Rsp(int socket_port, MemIF* mem, LogIF *log, std::list<DbgIF*> list_dbgif, std::list<DbgIF*> list_dbg_cluster_if, BreakPoints* bp, DbgIF *main_if);

    bool open();
    void close(int kill);


  private:

    bool decode(int socket_client, char* data, size_t len);
    bool get_packet(int socket_client, char* data, size_t* len);

    void client_routine(int socket_client);
    void listener_routine();


    bool regs_send(int socket_client);
    bool signal(int socket_client);

    bool multithread(int socket_client, char* data, size_t len);

    bool v_packet(int socket_client, char* data, size_t len);

    bool query(int socket_client, char* data, size_t len);

    bool send(int socket_client, const char* data, size_t len);
    bool send_str(int socket_client, const char* data);

    bool cont(int socket_client, char* data, size_t len); // continue, reserved keyword, thus not used as function name
    bool resume(int socket_client, bool step);
    bool resume(int socket_client, int tid, bool step);
    bool wait(int socket_client, Target_core *core=NULL);

    // internal helper functions
    bool pc_read(int socket_client, unsigned int* pc);

    bool reg_read(int socket_client, char* data, size_t len);
    bool reg_write(int socket_client, char* data, size_t len);

    bool mem_read(int socket_client, char* data, size_t len);
    bool mem_write_ascii(int socket_client, char* data, size_t len);
    bool mem_write(int socket_client, char* data, size_t len);


    Gdb_server *top;
    int socket_port;
    int socket_in;
    std::thread *listener_thread;

    int thread_sel;



    bool wait_client();
    bool loop();


    bool step(char* data, size_t len);


    //void resumeCoresPrepare(DbgIF *dbgif, bool step);
    void resumeCores();

    bool bp_insert(char* data, size_t len);
    bool bp_remove(char* data, size_t len);

    //DbgIF* get_dbgif(int thread_id);


    int m_thread_init;
    //MemIF* m_mem;
    //LogIF *log;
    //BreakPoints* m_bp;
    //std::list<DbgIF*> m_dbgifs;
    //std::list<DbgIF*> m_dbg_cluster_ifs;
};

#endif