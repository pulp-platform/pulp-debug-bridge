/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna and GreenWaves Technologies SA
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
 *          Martin Croome, GreenWaves Technologies (martin.croome@greenwaves-technologies.com)
 */


#include <list>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "gdb-server.hpp"

#define REPLY_BUF_LEN 256
#define PACKET_MAX_LEN 4096u

enum mp_type {
  BP_MEMORY   = 0,
  BP_HARDWARE = 1,
  WP_WRITE    = 2,
  WP_READ     = 3,
  WP_ACCESS   = 4
};



// Rsp

Rsp::Rsp(Gdb_server *top, int port) : top(top), port(port)
{
  init();
}

void Rsp::init()
{
  this->halt_target();
  main_core = top->target->get_threads().front();
  m_thread_init = main_core->get_thread_id();
}

void Rsp::client_connected(Tcp_socket::tcp_socket_ptr_t client)
{
  top->log->user("RSP: client connected\n");

  // Make sure target is halted
  this->halt_target();
  this->client = std::make_shared<Rsp::Client>(this, client);

  // hang the listener until the client stops (i.e. gdb session completed)
  {
    std::unique_lock<std::mutex> lk(m_rsp_client);
    cv_rsp_client.wait(lk, [this]{return !this->client->is_running(); });
  }

  top->log->user("RSP: client disconnected\n");
  this->halt_target();

  // If we're not aborted leave the target running when nothing is attached
  if (!this->aborted) {
    top->log->debug("RSP: clear breakpoints\n");
    // clear the breakpoints
    top->bkp->clear();

    // Start everything
    top->log->debug("RSP: resume target\n");
    this->resume_target(false);
  }

  top->log->debug("RSP: clean up client\n");

  // Clean up the client
  this->client->stop();
  top->log->debug("RSP: delete client\n");
  this->client = nullptr;

  top->log->debug("RSP: notify finished\n");
  {
    std::unique_lock<std::mutex> lk(m_finished);
    cv_finished.notify_one();
  }
  top->log->debug("RSP: finished notified\n");
}

void Rsp::client_disconnected(Tcp_socket::tcp_socket_ptr_t)
{
  top->log->user("RSP: TCP client disconnected\n");
}

void Rsp::rsp_client_finished()
{
  top->log->print(LOG_INFO, "RSP: client finished!\n");
  // indicate that the client has finished
  std::unique_lock<std::mutex> lk(m_rsp_client);
  conn_cnt++;
  cv_rsp_client.notify_one();
}

void Rsp::wait_finished()
{
  std::unique_lock<std::mutex> lk(m_finished);
  cv_finished.wait(lk, [this]{ return this->aborted; });
}

void Rsp::close(bool wait_finished)
{
  if (this->client && this->client->is_worker_thread(std::this_thread::get_id())) {
    assert(!wait_finished); // wait_finished cannot be true if we are called from the RSP worker thread
    this->aborted = true;
    return;
  }
  if (wait_finished) {
    top->log->debug("RSP: Wait for RSP client to finish\n");
    this->wait_finished();
    top->log->debug("RSP: RSP client is finished\n");
  }
  if (this->client) {
    client->stop();
  }
  if (this->listener) {
    listener->stop();
    delete listener;
    listener = NULL;
  }
}


bool Rsp::open() {
  listener = new Tcp_listener(
    this->top->log,
    port,
    [this](Tcp_socket::tcp_socket_ptr_t client) { return this->client_connected(client); }, 
    [this](Tcp_socket::tcp_socket_ptr_t client) { return this->client_disconnected(client); }
  );
  return listener->start();
}

void Rsp::indicate_resume()
{
  this->top->cmd_cb("__gdb_tgt_res", NULL, 0);
}



void Rsp::indicate_halt()
{
  this->top->cmd_cb("__gdb_tgt_hlt", NULL, 0);
}



void Rsp::halt_target()
{
  this->indicate_halt();
  this->top->target->halt();
}



void Rsp::resume_target(bool step, int tid)
{
  this->top->target->clear_resume_all();
  if (tid > 0)
    this->top->target->get_thread(tid)->prepare_resume(step);
  else
    this->top->target->prepare_resume_all(step);
  this->indicate_resume();
  this->top->target->resume_all();
}


// Rsp::Client

Rsp::Client::Client(Rsp *rsp, Tcp_socket::tcp_socket_ptr_t client) : top(rsp->top), rsp(rsp), client(client)
{
  thread_sel = rsp->m_thread_init;
  thread = new std::thread(&Rsp::Client::client_routine, this);
}

void Rsp::Client::stop()
{
  top->log->debug("RSP client stopping\n");
  this->client->close();
  top->log->debug("RSP client joining\n");
  thread->join();
  top->log->debug("RSP client joined\n");
}



void Rsp::Client::client_routine()
{
  while(running && !rsp->aborted)
  {
    char pkt[PACKET_MAX_LEN];
    size_t len;

    len = this->get_packet(pkt, (size_t) PACKET_MAX_LEN);

    running = len > 0;

    if (running && !rsp->aborted) {
      running = this->decode(pkt, len);
      if (!running) {
        client->close();
      }
    }
  }
  // close the connection when aborted
  if (running) {
    client->close();
    running = false;
  }
  top->log->debug("RSP client routine finished\n");
  rsp->rsp_client_finished();
}



bool Rsp::Client::v_packet(char* data, size_t len)
{
  top->log->print(LOG_DEBUG, "V Packet: %s\n", data);
  if (strncmp ("vKill", data, std::min(strlen ("vKill"), len)) == 0)
  {
    rsp->halt_target();
    return this->send_str("OK");
  }
  // else if (strncmp ("vRun", data, strlen ("vRun")) == 0)
  // {
  //   char *filename = &data[5];
  //   top->log->print(LOG_DEBUG, "Run: %s\n", filename);
  //   return this->send_str("X09;process:a410");
  // }
  else if (strncmp ("vCont?", data, std::min(strlen ("vCont?"), len)) == 0)
  {
    return this->send_str("vCont;c;s;C;S");
  }
  else if (strncmp ("vCont", data, std::min(strlen ("vCont"), len)) == 0)
  {

    this->top->target->clear_resume_all();

    // vCont can contains several commands, handle them in sequence
    // TODO - this could use strtok_s to be more robust
    char *str = strtok(&data[6], ";");
    while(str != NULL) {
      // Extract command and thread ID
      char *delim = strchr(str, ':');
      int tid = -1;
      if (delim != NULL) {
        tid = atoi(delim+1);
        if (tid == 0) {
          tid = 1;
        } else {
          tid = tid - 1;
        }
        *delim = 0;
        thread_sel = tid;
      }

      bool cont = false;
      bool step = false;

      if (str[0] == 'C' || str[0] == 'c') {
        cont = true;
        step = false;
      } else if (str[0] == 'S' || str[0] == 's') {
        cont = true;
        step = true;
      } else {
        top->log->print(LOG_ERROR, "Unsupported command in vCont packet: %s\n", str);
        exit(-1);
      }

      if (cont) {
        if (tid == -1) {
          this->top->target->prepare_resume_all(step);
        } else {
          this->top->target->get_thread(tid)->prepare_resume(step);
        }
      }

      str = strtok(NULL, ";");
    }

    rsp->indicate_resume();
    this->top->target->resume_all();

    return this->wait();
  }

  return this->send_str("");
}



bool Rsp::Client::query(char* data, size_t len)
{
  int ret;
  char reply[REPLY_BUF_LEN];
  top->log->print(LOG_DEBUG, "Query packet: %s\n", data);
  if (strncmp ("qSupported", data, strlen ("qSupported")) == 0)
  {
    Rsp_capability::parse(data, len, &remote_caps);
    top->log->debug("swbreak: %d\n", this->remote_capability("swbreak"));
    if (strlen(top->capabilities) > 0) {
      snprintf(reply, REPLY_BUF_LEN, "PacketSize=%x;%s", REPLY_BUF_LEN, top->capabilities);
    } else {
      snprintf(reply, REPLY_BUF_LEN, "PacketSize=%x", REPLY_BUF_LEN);
    }
    return this->send_str(reply);
  }
  else if (strncmp ("qTStatus", data, strlen ("qTStatus")) == 0)
  {
    // not supported, send empty packet
    return this->send_str("");
  }
  else if (strncmp ("qfThreadInfo", data, strlen ("qfThreadInfo")) == 0)
  {
    reply[0] = 'm';
    ret = 1;
    for (auto &thread : top->target->get_threads())
    {
      ret += snprintf(&reply[ret], REPLY_BUF_LEN - ret, "%u,", thread->get_thread_id()+1);
    } 

    return this->send(reply, ret-1);
  }
  else if (strncmp ("qsThreadInfo", data, strlen ("qsThreadInfo")) == 0)
  {
    return this->send_str("l");
  }
  else if (strncmp ("qThreadExtraInfo", data, strlen ("qThreadExtraInfo")) == 0)
  {
    const char* str_default = "Unknown Core";
    char str[REPLY_BUF_LEN];
    unsigned int thread_id;
    if (sscanf(data, "qThreadExtraInfo,%x", &thread_id) != 1) {
      top->log->print(LOG_ERROR, "Could not parse qThreadExtraInfo packet\n");
      return this->send_str("");
    }
    Target_core *thread = top->target->get_thread(thread_id - 1);
    {
      if (thread != NULL)
        thread->get_name(str, REPLY_BUF_LEN);
      else
        strcpy(str, str_default);

      ret = 0;
      for(size_t i = 0; i < strlen(str); i++)
        ret += snprintf(&reply[ret], REPLY_BUF_LEN - ret, "%02X", str[i]);
    }

    return this->send(reply, ret);
  }
  else if (strncmp ("qAttached", data, strlen ("qAttached")) == 0)
  {
    if (top->target->is_stopped()) {
      return this->send_str("0");
    } else {
      return this->send_str("1");
    }
  }
  else if (strncmp ("qC", data, strlen ("qC")) == 0)
  {
    snprintf(reply, 64, "0.%u", this->top->target->get_thread(thread_sel)->get_thread_id()+1);
    return this->send_str(reply);
  }
  else if (strncmp ("qSymbol", data, strlen ("qSymbol")) == 0)
  {
    return this->send_str("OK");
  }
  else if (strncmp ("qOffsets", data, strlen ("qOffsets")) == 0)
  {
    return this->send_str("Text=0;Data=0;Bss=0");
  }
  else if (strncmp ("qT", data, strlen ("qT")) == 0)
  {
    // not supported, send empty packet
    return this->send_str("");
  }
  else if (strncmp ("qRcmd", data, strlen ("qRcmd")) == 0||strncmp ("qXfer", data, strlen ("qXfer")) == 0)
  {
    int ret = this->top->cmd_cb(data, reply, REPLY_BUF_LEN);
    if (ret > 0) {
      return this->send_str(reply);
    } else {
      return this->send_str("");
    }
  }

  top->log->print(LOG_ERROR, "Unknown query packet\n");

  return this->send_str("");
}


bool Rsp::Client::mem_read(char* data, size_t)
{
  unsigned char buffer[512];
  char reply[512];
  uint32_t addr;
  uint32_t length;
  uint32_t rdata;
  uint32_t i;

  if (sscanf(data, "%x,%x", &addr, &length) != 2) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  top->target->mem_read(addr, length, (char *)buffer);

  for(i = 0; i < length; i++) {
    rdata = buffer[i];
    snprintf(&reply[i * 2], 3, "%02x", rdata);
  }

  return this->send(reply, length*2);
}



bool Rsp::Client::mem_write_ascii(char* data, size_t len)
{
  uint32_t addr;
  int length;
  uint32_t wdata;
  size_t i, j;

  char* buffer;
  int buffer_len;

  if (sscanf(data, "%x,%d:", &addr, &length) != 2) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  for(i = 0; i < len; i++) {
    if (data[i] == ':') {
      break;
    }
  }

  if (i == len)
    return false;

  // align to hex data
  data = &data[i+1];
  len = len - i - 1;

  buffer_len = len/2;
  buffer = (char*)malloc(buffer_len);
  if (buffer == NULL) {
    top->log->print(LOG_ERROR, "Failed to allocate buffer\n");
    return false;
  }

  for(j = 0; j < len/2; j++) {
    wdata = 0;
    for(i = 0; i < 2; i++) {
      char c = data[j * 2 + i];
      uint32_t hex = 0;
      if (c >= '0' && c <= '9')
        hex = c - '0';
      else if (c >= 'a' && c <= 'f')
        hex = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        hex = c - 'A' + 10;

      wdata |= hex << (4 * i);
    }

    buffer[j] = wdata;
  }

  top->target->mem_write(addr, buffer_len, buffer);

  free(buffer);

  return this->send_str("OK");
}

bool Rsp::Client::mem_write(char* data, size_t len)
{
  uint32_t addr;
  unsigned int length;
  size_t i;

  if (sscanf(data, "%x,%x:", &addr, &length) != 2) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  for(i = 0; i < len; i++) {
    if (data[i] == ':') {
      break;
    }
  }

  if (i == len)
    return false;

  // align to hex data
  data = &data[i+1];
  len = len - i - 1;

  top->target->mem_write(addr, len, data);

  return this->send_str("OK");
}



bool Rsp::Client::reg_read(char* data, size_t)
{
  uint32_t addr;
  uint32_t rdata = 0;
  char data_str[10];

  if (sscanf(data, "%x", &addr) != 1) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  if (addr < 32)
    this->top->target->get_thread(thread_sel)->gpr_read(addr, &rdata);
  else if (addr == 0x20)
    this->top->target->get_thread(thread_sel)->actual_pc_read(&rdata);
  else if (addr >= 0x41) // Read CSR
    if (addr == 0x41 + 0x301) {
      top->log->debug("READ MISA ---------------------------\n");
      rdata = 0x04000000;
    } else {
      this->top->target->get_thread(thread_sel)->csr_read(addr - 0x41, &rdata);
    }
  else
    return this->send_str("");

  rdata = htonl(rdata);
  snprintf(data_str, 9, "%08x", rdata);

  return this->send_str(data_str);
}



bool Rsp::Client::reg_write(char* data, size_t)
{
  uint32_t addr;
  uint32_t wdata;
  Target_core *core;

  if (sscanf(data, "%x=%08x", &addr, &wdata) != 2) {
    top->log->error("Could not parse packet\n");
    return false;
  }

  wdata = ntohl(wdata);

  core = this->top->target->get_thread(thread_sel);
  if (addr < 32)
    core->gpr_write(addr, wdata);
  else if (addr == 32)
    core->write(DBG_NPC_REG, wdata);
  else
    return this->send_str("E01");

  return this->send_str("OK");
}



bool Rsp::Client::regs_send()
{
  uint32_t gpr[32];
  uint32_t pc = 0;
  char regs_str[512];
  int i;
  Target_core * core;

  memset(gpr, 0, sizeof(gpr));

  core = this->top->target->get_thread(thread_sel);

  core->gpr_read_all(gpr);

  // now build the string to send back
  for(i = 0; i < 32; i++) {
    snprintf(&regs_str[i * 8], 9, "%08x", (unsigned int)htonl(gpr[i]));
  }
  core->actual_pc_read(&pc);
  snprintf(&regs_str[32 * 8 + 0 * 8], 9, "%08x", (unsigned int)htonl(pc));

  return this->send_str(regs_str);
}

bool Rsp::Client::signal(Target_core *core)
{
  char str[128];
  int len;
  if (core == NULL) {
    len = snprintf(str, 128, "S%02x", this->get_signal(this->top->target->get_thread(thread_sel)));
  } else {
    int sig = this->get_signal(core);
    len = snprintf(str, 128, "T%02xthread:%1x;", sig, core->get_thread_id()+1);
  }
  
  return this->send(str, len);
}

int Rsp::Client::cause_to_signal(uint32_t cause, int * int_num)
{
  int res;
  if (EXC_CAUSE_INTERUPT(cause)) {
    if (int_num) {
      *int_num = cause & 0x1f;
    }
    res = TARGET_SIGNAL_INT;
  } else {
    cause &= EXC_CAUSE_MASK;
    if (cause == EXC_CAUSE_BREAKPOINT)
      res = TARGET_SIGNAL_TRAP;
    else if (cause == EXC_CAUSE_ILLEGAL_INSN)
      res = TARGET_SIGNAL_ILL;
    // else if ((cause & 0x1f) == 5) - There is no definition for this in the RTL
    //   return TARGET_SIGNAL_BUS;
    else
      res = TARGET_SIGNAL_STOP;
  }
  return res;
}

int Rsp::Client::get_signal(Target_core *core)
{
  if (core->is_stopped()) {
    bool is_hit, is_sleeping;
    if (!core->read_hit(&is_hit, &is_sleeping))
      return TARGET_SIGNAL_NONE;
    if (is_hit)
      return TARGET_SIGNAL_TRAP;
    else if (is_sleeping)
      return TARGET_SIGNAL_NONE;
    else
      return this->cause_to_signal(core->get_cause());
  } else {
    return TARGET_SIGNAL_NONE;
  }
}

bool Rsp::Client::cont(char* data, size_t)
{
  uint32_t sig;
  uint32_t addr;
  uint32_t npc;
  bool npc_found = false;
  Target_core *core;

  // strip signal first
  if (data[0] == 'C') {
    if (sscanf(data, "C%X;%X", &sig, &addr) == 2)
      npc_found = true;
  } else {
    if (sscanf(data, "c%X", &addr) == 1)
      npc_found = true;
  }

  if (npc_found) {
    core = this->top->target->get_thread(thread_sel);
    // only when we have received an address
    core->read(DBG_NPC_REG, &npc);

    if (npc != addr)
      core->write(DBG_NPC_REG, addr);
  }

  thread_sel = rsp->m_thread_init;

  rsp->resume_target(false);
  return this->wait();
}



// This should not be used and is probably wrong
// but GDB should use vCont anyway
bool Rsp::Client::step(char* data, size_t len)
{
  uint32_t addr;
  uint32_t npc;
  size_t i;
  Target_core *core;

  if (len < 1) return false;

  // strip signal first
  if (data[0] == 'S') {
    for (i = 0; i < len; i++) {
      if (data[i] == ';') {
        data = &data[i+1];
        break;
      }
    }
  }

  if (sscanf(data, "%x", &addr) == 1) {
    core = this->top->target->get_thread(thread_sel);
    // only when we have received an address
    core->read(DBG_NPC_REG, &npc);

    if (npc != addr)
      core->write(DBG_NPC_REG, addr);
  }

  thread_sel = rsp->m_thread_init;

  rsp->resume_target(true);
  return this->wait();
}



bool Rsp::Client::wait()
{
  int ret;
  char pkt;

  while(1) {
    // Moved into target
    // // Check if a cluster power state has changed
    // this->top->target->update_power();

    Target_core *stopped_core = this->top->target->check_stopped();

    if (stopped_core) {
      // move to thread of stopped core
      thread_sel = stopped_core->get_thread_id();
      this->top->log->debug("found stopped core - thread %d\n", thread_sel + 1);
      rsp->halt_target();
      return this->signal(stopped_core);
    }

    // Otherwise wait for a stop request from gdb side for a while
    ret = this->client->receive(&pkt, 1, 100, false);

    if (ret < 0) {
      return false;
    } else if (ret == 1 && pkt == 0x3) {
      this->top->log->debug("!!!!!!!!!!!!!!!!!!!!!!! CTRL-C !!!!!!!!!!!!!!!!!!!!!!!!\n");
      rsp->halt_target();
    }
    // usleep(10000);
  }

  return true;
}



bool Rsp::Client::multithread(char* data, size_t len)
{
  int thread_id;
  top->log->debug("Subsequent %c operations on thread %s\n", data[0], &data[1]);

  if (len < 1) return false;

  switch (data[0]) {
    case 'c':
    case 'g':

      if (sscanf(&data[1], "%d", &thread_id) != 1)
        return false;

      if (thread_id == -1) // affects all threads
        return this->send_str("OK");

      if (thread_id != 0)
        thread_id = thread_id - 1;

      // we got the thread id, now let's look for this thread in our list
      if (this->top->target->get_thread(thread_id) != NULL) {
        thread_sel = thread_id;
        return this->send_str("OK");
      }

      return this->send_str("E01");
  }

  return false;
}



bool Rsp::Client::decode(char* data, size_t len)
{
  if (data[0] == 0x03) {
    top->log->print(LOG_DEBUG, "Received break\n");
    return this->signal();
  }

  top->log->print(LOG_DEBUG, "Received %c command (len: %zd)\n", data[0], len);
  switch (data[0]) {
  case 'q':
    return this->query(data, len);

  case 'g':
    return this->regs_send();

  case 'p':
    return this->reg_read(&data[1], len-1);

  case 'P':
    return this->reg_write(&data[1], len-1);

  case 'c':
  case 'C':
    return this->cont(&data[0], len);

  case 's':
  case 'S':
    return this->step(&data[0], len);

  case 'H':
    return this->multithread(&data[1], len-1);

  case 'm':
    return this->mem_read(&data[1], len-1);

  case '?':
    return this->signal();

  case 'v':
    return this->v_packet(&data[0], len);

  case 'M':
    return this->mem_write_ascii(&data[1], len-1);

  case 'X':
    return this->mem_write(&data[1], len-1);

  case 'z':
    return this->bp_remove(&data[0], len);

  case 'Z':
    return this->bp_insert(&data[0], len);

  case 'T':
    return this->send_str("OK"); // threads are always alive

  case 'D':
    this->send_str("OK");
    return false;

  // case '!':
  //   return this->send_str("OK"); // extended mode supported

  default:
    top->log->print(LOG_ERROR, "Unknown packet: starts with %c\n", data[0]);
    break;
  }

  return false;
}

#ifdef _WIN32
inline void timersub(const timeval* tvp, const timeval* uvp, timeval* vvp)
{
  vvp->tv_sec = tvp->tv_sec - uvp->tv_sec;
  vvp->tv_usec = tvp->tv_usec - uvp->tv_usec;
  if (vvp->tv_usec < 0)
  {
     --vvp->tv_sec;
     vvp->tv_usec += 1000000;
  }
}
#endif

bool time_has_expired(const timeval* start, const timeval* max_delay)
{
  struct timeval now, used;
  ::gettimeofday(&now, NULL);
  timersub(&now, start, &used);        
  return timercmp(max_delay, &used, <);
}

bool verify_checksum(const char * buf, size_t hash_pos)
{
  unsigned int checksum = 0;
  for(size_t i = 0; i < hash_pos; i++) {
    checksum += buf[i];
  }

  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);
  return (buf[hash_pos + 1] == checksum_str[0] && buf[hash_pos + 2] == checksum_str[1]);
}

size_t deescape(char * buf, size_t len)
{
  size_t i = 0, cur = 0;
  bool escaped = false;
  while (i < len) {
    if (escaped) {
      escaped = false;
      buf[cur++] = buf[i++] ^ 0x20;
    } else if (buf[i] == '}') {
      escaped = true;
      i++;
    } else {
      if (i != cur) buf[cur] = buf[i];
      i++; cur++;
    }
  }
  buf[cur] = 0;
  return cur;
}

bool scan_for_hash(const char *pkt, size_t *cur, bool *escaped, size_t last)
{
  while (*cur < last) {
    if (*escaped) {
      *escaped = false;
    } else if (pkt[*cur] == '}') {
      *escaped = true;
    } else if (pkt[*cur] == '#') {
      return true;
    }
    (*cur)++;
  }
  return false;
}

enum pkt_rcv_states {
  STATE_INIT = 0,
  STATE_LEADIN,
  STATE_BODY,
  STATE_CRC,
  STATE_ACKNOWLEDGE
};

const char *pkt_rcv_states_str[] =
{
    "STATE_INIT",
    "STATE_LEADIN",
    "STATE_BODY",
    "STATE_CRC",
    "STATE_ACKNOWLEDGE"
};

size_t Rsp::Client::get_packet(char* pkt, size_t max_pkt_len) {
  // packets follow the format: $packet-data#checksum
  // checksum is two-digit

  top->log->debug("get packet called\n");
  size_t cur = 1, last = 1, crc_start = -1, pkt_len;
  struct timeval start;
  bool escaped = false;
  int ret;
  char c;
  pkt_rcv_states state = STATE_INIT;

  struct timeval max_delay;
  max_delay.tv_sec = (packet_timeout * 1000) / 1000000;
  max_delay.tv_usec = (packet_timeout * 1000) % 1000000;

  while (1) {
    switch(state) {
      case STATE_INIT:
        memset(pkt, 0, max_pkt_len);
        state = STATE_LEADIN; cur = last = pkt_len = 0; crc_start = -1;
        escaped = false;
        break;
      case STATE_LEADIN:
        ret = client->receive(&c, 1, packet_timeout, true);
        if (ret == SOCKET_ERROR) return 0;
        if (ret > 0) {
          switch(c) {
            case 0x03:
              return 1; // special case for 0x03 (asynchronous break)
            case '$':
              state = STATE_BODY;
              ::gettimeofday(&start, NULL);
              break;
          }
        }
        break;
      case STATE_CRC:
        if (cur - crc_start >= 3) {
          if (verify_checksum(pkt, crc_start)) {
            pkt_len = deescape(pkt, crc_start);
            state = STATE_ACKNOWLEDGE;
          } else {
            top->log->error("RSP: Packet CRC error\n");
            state = STATE_INIT;
          }
          break;
        }
        /* fall through. */
      case STATE_BODY:
        ret = this->client->receive(&(pkt[cur]), (max_pkt_len - cur), 100, false);

        if (ret == SOCKET_ERROR) return 0;

        if (ret > 0) {
          last = cur + ret;
          if (state == STATE_BODY) {
            if (scan_for_hash(pkt, &cur, &escaped, last)) {
              crc_start = cur; cur = last; state = STATE_CRC;
              break;
            }
          } else cur = last;
        }
        if (state == STATE_BODY || state == STATE_CRC) {
          if (cur >= max_pkt_len) {
            top->log->error("RSP: Too many characters received\n");
            state = STATE_INIT;
          } else if (time_has_expired(&start, &max_delay)) {
            // no more time - look for another packet
            state = STATE_INIT;
          }
        }
        break;
      case STATE_ACKNOWLEDGE:
        if (this->client->send("+", 1) != 1) {
          top->log->error("RSP: Sending ACK failed\n");
          return 0;
        } else {
          return pkt_len;
        }
    }
  }
}

bool Rsp::Client::send(const char* data, size_t len)
{
  int ret;
  size_t i;
  size_t raw_len = 0;
  char* raw = (char*)malloc(len * 2 + 4);
  unsigned int checksum = 0;

  raw[raw_len++] = '$';

  for (i = 0; i < len; i++) {
    char c = data[i];

    // check if escaping needed
    if (c == '#' || c == '%' || c == '}' || c == '*') {
      raw[raw_len++] = '}';
      raw[raw_len++] = c;
      checksum += '}';
      checksum += c;
    } else {
      raw[raw_len++] = c;
      checksum += c;
    }
  }

  // add checksum
  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  raw[raw_len++] = '#';
  raw[raw_len++] = checksum_str[0];
  raw[raw_len++] = checksum_str[1];

  char ack;
  do {
    top->log->print(LOG_DEBUG, "Sending %.*s\n", raw_len, raw);

    if (client->send(raw, raw_len) == SOCKET_ERROR) {
      free(raw);
      top->log->print(LOG_ERROR, "Unable to send data to client\n");
      return false;
    }

    ret = client->receive(&ack, 1, 1000, true);
    if(ret == SOCKET_ERROR) {
      free(raw);
      top->log->print(LOG_ERROR, "RSP: Error receiving0\n");
      return false;
    }
    top->log->print(LOG_DEBUG, "Received %c\n", ack);

    if(ret == 0) {
      // no data available
      continue;
    }

  } while (ack != '+');

  free(raw);
  return true;
}

bool Rsp::Client::send_str(const char* data)
{
  return this->send(data, strlen(data));
}

bool Rsp::Client::bp_insert(char* data, size_t len)
{
  enum mp_type type;
  uint32_t addr;
  int bp_len;

  if (len < 1) return false;

  if (3 != sscanf(data, "Z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    top->log->error("Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    top->log->error("ERROR: Not a memory bp\n");
    this->send_str("");
    return false;
  }

  if (!top->bkp->insert(addr)) {
    top->log->error("Unable to insert breakpoint\n");
    return false;
  }

  top->log->debug("Breakpoint inserted at 0x%08x\n", addr);
  return this->send_str("OK");
}



bool Rsp::Client::bp_remove(char* data, size_t len)
{
  enum mp_type type;
  uint32_t addr;
  int bp_len;

  data[len] = 0;

  if (3 != sscanf(data, "z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    top->log->print(LOG_ERROR, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    top->log->print(LOG_ERROR, "Not a memory bp\n");
    return false;
  }

  if (!top->bkp->remove(addr)) {
    top->log->error("Unable to remove breakpoint\n");
    return false;
  }

  return this->send_str("OK");
}

Rsp_capability::Rsp_capability(const char * name, capability_support support) : name(name), support(support)
{
}

Rsp_capability::Rsp_capability(const char * name, const char * value) : name(name), value(value), support(CAPABILITY_IS_SUPPORTED)
{
}

char *strnstr(char *str, const char *substr, size_t n)
{
    char *p = str, *pEnd = str+n;
    size_t substr_len = strlen(substr);

    if(0 == substr_len)
        return str; // the empty string is contained everywhere.

    pEnd -= (substr_len - 1);
    for(;p < pEnd; ++p)
    {
        if(0 == strncmp(p, substr, substr_len))
            return p;
    }
    return NULL;
}

void Rsp_capability::parse(char * buf, size_t len, Rsp_capabilities * caps)
{
  char * caps_buf = strnstr(buf, ":", len);
  if (!caps_buf)
    return;

  caps_buf++;
  // ensure terminated
  len = strnlen(caps_buf, len - (caps_buf - buf));
  caps_buf[len - 1] = 0;
  
  char * cap = strtok (caps_buf, ";");

  while (cap != NULL) {
    int last = (strlen(cap)-1);
    char cap_type = cap[last];

    switch (cap_type) {
      case '+':
        cap[last] = 0;
        caps->insert(
          std::make_pair(
            cap,
            unique_ptr<Rsp_capability>(new Rsp_capability(cap, CAPABILITY_IS_SUPPORTED))
          )
        );
        break;
      case '-':
        cap[last] = 0;
        caps->insert(
          std::make_pair(
            cap,
            unique_ptr<Rsp_capability>(new Rsp_capability(cap, CAPABILITY_NOT_SUPPORTED))
          )
        );
        break;
      case '?':
        cap[last] = 0;
        caps->insert(
          std::make_pair(
            cap,
            unique_ptr<Rsp_capability>(new Rsp_capability(cap, CAPABILITY_MAYBE_SUPPORTED))
          )
        );
        break;
      default:
        char * value = strstr(cap, "=");
        if (value) {
          value = 0;
          value++;
          caps->insert(
            std::make_pair(
              cap,
              unique_ptr<Rsp_capability>(new Rsp_capability(cap, value))
            )
          );
        }
        break;
    }
    cap = strtok (NULL, ";");
  }
}