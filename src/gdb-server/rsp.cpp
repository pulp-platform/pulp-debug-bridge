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
 *          Martin Croome, GreenWaves Technologies (martin.croome@greenwaves-technologies.com)
 */


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
#include "gdb-server.hpp"
#include <unistd.h>

#define REPLY_BUF_LEN 256

enum mp_type {
  BP_MEMORY   = 0,
  BP_HARDWARE = 1,
  WP_WRITE    = 2,
  WP_READ     = 3,
  WP_ACCESS   = 4
};

#define PACKET_MAX_LEN 4096

Rsp::Rsp(Gdb_server *top, int socket_port) : top(top), socket_port(socket_port), stopped(false)
{
  init();
}

void Rsp::init()
{
  main_core = top->target->get_threads().front();

  m_thread_init = main_core->get_thread_id();
  thread_sel = m_thread_init;
}

bool Rsp::v_packet(int socket_client, char* data, size_t len)
{
  top->log->print(LOG_DEBUG, "V Packet: %s\n", data);
  if (strncmp ("vKill", data, strlen ("vKill")) == 0)
  {
    this->top->target->halt();
    stopped=true;
    return this->send_str(socket_client,  "OK");
  }
  else if (strncmp ("vRun", data, strlen ("vRun")) == 0)
  {
    char *filename = &data[5];
    top->log->print(LOG_DEBUG, "Run: %s\n", filename);
    return this->send_str(socket_client,  "X09;process:a410");
  }
  else if (strncmp ("vCont?", data, strlen ("vCont?")) == 0)
  {
    return this->send_str(socket_client,  "vCont;c;s;C;S");
  }
  else if (strncmp ("vCont", data, strlen ("vCont")) == 0)
  {

    this->top->target->clear_resume_all();

    // vCont can contains several commands, handle them in sequence
    char *str = strtok(&data[6], ";");
    while(str != NULL) {
      // Extract command and thread ID
      char *delim = index(str, ':');
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

    this->top->target->resume_all();

    return this->wait(socket_client);
  }

  return this->send_str(socket_client,  "");
}

bool Rsp::query(int socket_client, char* data, size_t len)
{
  int ret;
  char reply[REPLY_BUF_LEN];
  top->log->print(LOG_DEBUG, "Query packet: %s\n", data);
  if (strncmp ("qSupported", data, strlen ("qSupported")) == 0)
  {
    if (strlen(top->capabilities) > 0) {
      snprintf(reply, REPLY_BUF_LEN, "PacketSize=%x;%s", REPLY_BUF_LEN, top->capabilities);
    } else {
      snprintf(reply, REPLY_BUF_LEN, "PacketSize=%x", REPLY_BUF_LEN);
    }
    return this->send_str(socket_client, reply);
  }
  else if (strncmp ("qTStatus", data, strlen ("qTStatus")) == 0)
  {
    // not supported, send empty packet
    return this->send_str(socket_client,  "");
  }
  else if (strncmp ("qfThreadInfo", data, strlen ("qfThreadInfo")) == 0)
  {
    reply[0] = 'm';
    ret = 1;
    for (auto &thread : top->target->get_threads())
    {
      ret += snprintf(&reply[ret], REPLY_BUF_LEN - ret, "%u,", thread->get_thread_id()+1);
    } 

    return this->send(socket_client, reply, ret-1);
  }
  else if (strncmp ("qsThreadInfo", data, strlen ("qsThreadInfo")) == 0)
  {
    return this->send_str(socket_client,  "l");
  }
  else if (strncmp ("qThreadExtraInfo", data, strlen ("qThreadExtraInfo")) == 0)
  {
    const char* str_default = "Unknown Core";
    char str[REPLY_BUF_LEN];
    unsigned int thread_id;
    if (sscanf(data, "qThreadExtraInfo,%d", &thread_id) != 1) {
      top->log->print(LOG_ERROR, "Could not parse qThreadExtraInfo packet\n");
      return this->send_str(socket_client,  "");
    }
    Target_core *thread = top->target->get_thread(thread_id - 1);
    {
      if (thread != NULL)
        thread->get_name(str, REPLY_BUF_LEN);
      else
        strcpy(str, str_default);

      ret = 0;
      for(int i = 0; i < strlen(str); i++)
        ret += snprintf(&reply[ret], REPLY_BUF_LEN - ret, "%02X", str[i]);
    }

    return this->send(socket_client, reply, ret);
  }
  else if (strncmp ("qAttached", data, strlen ("qAttached")) == 0)
  {
    if (stopped) {
      return this->send_str(socket_client,  "0");
    } else {
      return this->send_str(socket_client,  "1");
    }
  }
  else if (strncmp ("qC", data, strlen ("qC")) == 0)
  {
    snprintf(reply, 64, "0.%u", this->top->target->get_thread(thread_sel)->get_thread_id()+1);
    return this->send_str(socket_client,  reply);
  }
  else if (strncmp ("qSymbol", data, strlen ("qSymbol")) == 0)
  {
    return this->send_str(socket_client,  "OK");
  }
  else if (strncmp ("qOffsets", data, strlen ("qOffsets")) == 0)
  {
    return this->send_str(socket_client,  "Text=0;Data=0;Bss=0");
  }
  else if (strncmp ("qT", data, strlen ("qT")) == 0)
  {
    // not supported, send empty packet
    return this->send_str(socket_client,  "");
  }
  else if (strncmp ("qRcmd", data, strlen ("qRcmd")) == 0||strncmp ("qXfer", data, strlen ("qXfer")) == 0)
  {
    int ret = this->top->cmd_cb(data, reply, REPLY_BUF_LEN);
    if (ret > 0) {
      return this->send_str(socket_client, reply);
    } else {
      return this->send_str(socket_client, "");
    }
  }

  top->log->print(LOG_ERROR, "Unknown query packet\n");

  return this->send_str(socket_client, "");
}


bool Rsp::mem_read(int socket_client, char* data, size_t len)
{
  unsigned char buffer[512];
  char reply[512];
  uint32_t addr;
  uint32_t length;
  uint32_t rdata;
  int i;

  if (sscanf(data, "%x,%x", &addr, &length) != 2) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  top->cable->access(false, addr, length, (char *)buffer);

  for(i = 0; i < length; i++) {
    rdata = buffer[i];
    snprintf(&reply[i * 2], 3, "%02x", rdata);
  }

  return this->send(socket_client, reply, length*2);
}



bool Rsp::mem_write_ascii(int socket_client, char* data, size_t len)
{
  uint32_t addr;
  int length;
  uint32_t wdata;
  int i, j;

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

  top->cable->access(true, addr, buffer_len, buffer);

  free(buffer);

  return this->send_str(socket_client,  "OK");
}

bool Rsp::mem_write(int socket_client, char* data, size_t len)
{
  uint32_t addr;
  int length;
  uint32_t wdata;
  int i, j;

  char* buffer;
  int buffer_len;

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

  top->cable->access(true, addr, len, data);

  return this->send_str(socket_client,  "OK");
}



bool Rsp::reg_read(int socket_client, char* data, size_t len)
{
  uint32_t addr;
  uint32_t rdata;
  char data_str[10];

  if (sscanf(data, "%x", &addr) != 1) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  if (addr < 32)
    this->top->target->get_thread(thread_sel)->gpr_read(addr, &rdata);
  else if (addr == 0x20)
    this->top->target->get_thread(thread_sel)->actual_pc_read(&rdata);
  else if (addr == 0x41 + 0x301) // CSR MISA read - return not implemented
    return this->send_str(socket_client,  "0000");
  else
    return this->send_str(socket_client,  "");

  rdata = htonl(rdata);
  snprintf(data_str, 9, "%08x", rdata);

  return this->send_str(socket_client,  data_str);
}



bool Rsp::reg_write(int socket_client, char* data, size_t len)
{
  uint32_t addr;
  uint32_t wdata;
  char data_str[10];
  Target_core *core;

  if (sscanf(data, "%x=%08x", &addr, &wdata) != 2) {
    top->log->print(LOG_ERROR, "Could not parse packet\n");
    return false;
  }

  wdata = ntohl(wdata);

  core = this->top->target->get_thread(thread_sel);
  if (addr < 32)
    core->gpr_write(addr, wdata);
  else if (addr == 32)
    core->write(DBG_NPC_REG, wdata);
  else
    return this->send_str(socket_client,  "E01");

  return this->send_str(socket_client,  "OK");
}



bool Rsp::regs_send(int socket_client)
{
  uint32_t gpr[32];
  uint32_t npc;
  uint32_t ppc;
  char regs_str[512];
  int i;
  Target_core * core;

  core = this->top->target->get_thread(thread_sel);

  core->gpr_read_all(gpr);

  // now build the string to send back
  for(i = 0; i < 32; i++) {
    snprintf(&regs_str[i * 8], 9, "%08x", htonl(gpr[i]));
  }
  core->actual_pc_read(&npc);
  snprintf(&regs_str[32 * 8 + 0 * 8], 9, "%08x", htonl(npc));
  top->log->print(LOG_ERROR, "PC 0x%x Thread %d\n", npc, thread_sel);

  return this->send_str(socket_client,  regs_str);
}

bool Rsp::signal(int socket_client, Target_core *core)
{
  char str[16];
  int len;
  if (stopped) {
    return this->send_str(socket_client, "X00");
  }
  if (core == NULL) {
    len = snprintf(str, 16, "S%02x", this->get_signal(this->top->target->get_thread(thread_sel)));
  } else {
    int sig = this->get_signal(core);
    this->top->log->debug("sig is 0x%x\n", sig);
    len = snprintf(str, 16, "T%02xthread:%1x;", sig, core->get_thread_id()+1);
  }
  
  return this->send(socket_client, str, len);
}

int Rsp::cause_to_signal(uint32_t cause, int * int_num)
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

int Rsp::get_signal(Target_core *core)
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

bool Rsp::cont(int socket_client, char* data, size_t len)
{
  uint32_t sig;
  uint32_t addr;
  uint32_t npc;
  int i;
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

  thread_sel = m_thread_init;

  return this->resume(socket_client, false);
}



bool Rsp::resume(int socket_client, bool step)
{
  this->top->target->clear_resume_all();
  this->top->target->prepare_resume_all(step);
  this->top->target->resume_all();
  return this->wait(socket_client);
}



bool Rsp::resume(int socket_client, int tid, bool step)
{
  this->top->target->clear_resume_all();
  this->top->target->get_thread(tid)->prepare_resume(step);
  this->top->target->resume_all();
  return this->wait(socket_client);
}



bool Rsp::step(int socket_client, char* data, size_t len)
{
  uint32_t addr;
  uint32_t npc;
  int i;
  Target_core *core;

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

  thread_sel = m_thread_init;

  return this->resume(socket_client, true);
}



bool Rsp::wait(int socket_client)
{
  int ret;
  char pkt;

  fd_set rfds;
  struct timeval tv;

  while(1) {
    // Check if a cluster power state has changed
    this->top->target->update_power();

    Target_core *stopped_core = this->top->target->check_stopped();

    if (stopped_core) {
      // move to thread of stopped core
      thread_sel = stopped_core->get_thread_id();
      this->top->log->debug("found stopped core - thread %d\n", thread_sel);
      this->top->target->halt();
      return this->signal(socket_client, stopped_core);
    }

    // Otherwise wait for a stop request from gdb side for a while

    FD_ZERO(&rfds);
    FD_SET(socket_client, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;

    if (select(socket_client+1, &rfds, NULL, NULL, &tv)) {
      ret = recv(socket_client, &pkt, 1, 0);
      if (ret == 1 && pkt == 0x3) {
        top->target->halt();
      }
    }
  }

  return true;
}



bool Rsp::multithread(int socket_client, char* data, size_t len)
{
  int thread_id;

  switch (data[0]) {
    case 'c':
    case 'g':
      if (sscanf(&data[1], "%d", &thread_id) != 1)
        return false;

      if (thread_id == -1) // affects all threads
        return this->send_str(socket_client,  "OK");

      if (thread_id != 0)
        thread_id = thread_id - 1;

      // we got the thread id, now let's look for this thread in our list
      if (this->top->target->get_thread(thread_id) != NULL) {
        thread_sel = thread_id;
        return this->send_str(socket_client,  "OK");
      }

      return this->send_str(socket_client,  "E01");
  }

  return false;
}



bool Rsp::decode(int socket_client, char* data, size_t len)
{
  if (data[0] == 0x03) {
    top->log->print(LOG_DEBUG, "Received break\n");
    return this->signal(socket_client);
  }

  top->log->print(LOG_DEBUG, "Received %c command!\n", data[0]);
  switch (data[0]) {
  case 'q':
    return this->query(socket_client, &data[0], len);

  case 'g':
    return this->regs_send(socket_client);

  case 'p':
    return this->reg_read(socket_client, &data[1], len-1);

  case 'P':
    return this->reg_write(socket_client, &data[1], len-1);

  case 'c':
  case 'C':
    return this->cont(socket_client, &data[0], len);

  case 's':
  case 'S':
    return this->step(socket_client, &data[0], len);

  case 'H':
    return this->multithread(socket_client, &data[1], len-1);

  case 'm':
    return this->mem_read(socket_client, &data[1], len-1);

  case '?':
    return this->signal(socket_client);

  case 'v':
    return this->v_packet(socket_client, &data[0], len);

  case 'M':
    return this->mem_write_ascii(socket_client, &data[1], len-1);

  case 'X':
    return this->mem_write(socket_client, &data[1], len-1);

  case 'z':
    return this->bp_remove(socket_client, &data[0], len);

  case 'Z':
    return this->bp_insert(socket_client, &data[0], len);

  case 'T':
    return this->send_str(socket_client,  "OK"); // threads are always alive

  case 'D':
    this->send_str(socket_client,  "OK");
    return false;

  case '!':
    return this->send_str(socket_client,  "OK"); // extended mode supported

  default:
    top->log->print(LOG_ERROR, "Unknown packet: starts with %c\n", data[0]);
    break;
  }

  return false;
}

bool
Rsp::get_packet(int socket_client, char* pkt, size_t* p_pkt_len) {
  char c;
  char check_chars[2];
  char buffer[PACKET_MAX_LEN];
  int  buffer_len = 0;
  int  pkt_len;
  bool escaped = false;
  int ret;
  // packets follow the format: $packet-data#checksum
  // checksum is two-digit

  // poison packet
  memset(pkt, 0, PACKET_MAX_LEN);
  pkt_len = 0;

  // first look for start bit
  do {
    ret = recv(socket_client, &c, 1, 0);

    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      top->log->print(LOG_ERROR, "RSP: Error receiving1\n");
      exit(1);
      return false;
    }

    if(ret == -1 && errno == EWOULDBLOCK) {
      // no data available
      continue;
    }

    // special case for 0x03 (asynchronous break)
    if (c == 0x03) {
      pkt[0]  = c;
      *p_pkt_len = 1;
      return true;
    }
  } while(c != '$');

  buffer[0] = c;

  // now store data as long as we don't see #
  do {
    if (buffer_len >= PACKET_MAX_LEN || pkt_len >= PACKET_MAX_LEN) {
      top->log->print(LOG_ERROR, "RSP: Too many characters received\n");
      return false;
    }

    ret = recv(socket_client, &c, 1, 0);

    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      top->log->print(LOG_ERROR, "RSP: Error receiving2\n");
      exit(1);
      return false;
    }

    if(ret == -1 && errno == EWOULDBLOCK) {
      // no data available
      continue;
    }

    buffer[buffer_len++] = c;

    // check for 0x7d = '}'
    if (c == 0x7d) {
      escaped = true;
      continue;
    }

    if (escaped)
      pkt[pkt_len++] = c ^ 0x20;
    else
      pkt[pkt_len++] = c;

    escaped = false;
  } while(c != '#');

  buffer_len--;
  pkt_len--;

  // checksum, 2 bytes
  ret = recv(socket_client, &check_chars[0], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    top->log->print(LOG_ERROR, "RSP: Error receiving3\n");
    exit(1);
    return false;
  }

  ret = recv(socket_client, &check_chars[1], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    top->log->print(LOG_ERROR, "RSP: Error receiving4\n");
    exit(1);
    return false;
  }

  // check the checksum
  unsigned int checksum = 0;
  for(int i = 0; i < buffer_len; i++) {
    checksum += buffer[i];
  }

  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  if (check_chars[0] != checksum_str[0] || check_chars[1] != checksum_str[1]) {
    top->log->print(LOG_ERROR, "RSP: Checksum failed; received %.*s; checksum should be %02x\n", pkt_len, pkt, checksum);
    return false;
  }

  // now send ACK
  char ack = '+';
  if (::send(socket_client, &ack, 1, 0) != 1) {
    top->log->print(LOG_ERROR, "RSP: Sending ACK failed\n");
    return false;
  }

  // NULL terminate the string
  pkt[pkt_len] = '\0';
  *p_pkt_len = pkt_len;

  return true;
}

bool Rsp::send(int socket_client, const char* data, size_t len)
{
  int ret;
  int i;
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

    if (::send(socket_client, raw, raw_len, 0) != raw_len) {
      free(raw);
      top->log->print(LOG_ERROR, "Unable to send data to client\n");
      return false;
    }

    ret = recv(socket_client, &ack, 1, 0);
    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      free(raw);
      top->log->print(LOG_ERROR, "RSP: Error receiving0\n");
      exit(1);
      return false;
    }
    top->log->print(LOG_DEBUG, "Received %c\n", ack);

    if(ret == -1 && errno == EWOULDBLOCK) {
      // no data available
      continue;
    }

  } while (ack != '+');

  free(raw);
  return true;
}

bool Rsp::send_str(int socket_client, const char* data)
{
  return this->send(socket_client, data, strlen(data));
}

void Rsp::client_routine(int socket_client)
{
  while(1)
  {
    char pkt[PACKET_MAX_LEN];
    size_t len;

    fd_set rfds;
    struct timeval tv;

    while (this->get_packet(socket_client, pkt, &len)) {
      top->log->print(LOG_DEBUG, "Received $%.*s\n", len, pkt);
      if (!this->decode(socket_client, pkt, len)) {
        return;
      }
    }
  }
}

void Rsp::listener_routine()
{
  while(1)
  {
    int socket_client;

    if((socket_client = accept(socket_in, NULL, NULL)) == -1)
    {
      if(errno == EAGAIN)
        continue;

      top->log->print(LOG_ERROR, "Unable to accept connection: %s\n", strerror(errno));
      return;
    }

    top->log->print(LOG_INFO, "RSP: Client connected!\n");

    std::thread *thread = new std::thread(&Rsp::client_routine, this, socket_client);

  }
}

void Rsp::close(int kill)
{
  listener_thread->join();
}

bool
Rsp::open() {
  struct sockaddr_in addr;
  int yes = 1;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(socket_port);
  addr.sin_addr.s_addr = INADDR_ANY;
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  socket_in = socket(PF_INET, SOCK_STREAM, 0);
  if(socket_in < 0)
  {
    top->log->print(LOG_ERROR, "Unable to create comm socket: %s\n", strerror(errno));
    return false;
  }

  if(setsockopt(socket_in, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    top->log->print(LOG_ERROR, "Unable to setsockopt on the socket: %s\n", strerror(errno));
    return false;
  }

  if(bind(socket_in, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    top->log->print(LOG_ERROR, "Unable to bind the socket: %s\n", strerror(errno));
    return false;
  }

  if(listen(socket_in, 1) == -1) {
    top->log->print(LOG_ERROR, "Unable to listen: %s\n", strerror(errno));
    return false;
  }

  this->top->target->halt();

  listener_thread = new std::thread(&Rsp::listener_routine, this);

  top->log->print(LOG_INFO, "RSP server opened on port %d\n", socket_port);

  return true;
}



bool Rsp::bp_insert(int socket_client, char* data, size_t len)
{
  enum mp_type type;
  uint32_t addr;
  uint32_t data_bp;
  int bp_len;

  if (3 != sscanf(data, "Z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    top->log->print(LOG_ERROR, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    top->log->print(LOG_ERROR, "ERROR: Not a memory bp\n");
    this->send_str(socket_client,  "");
    return false;
  }

  top->bkp->insert(addr);

  return this->send_str(socket_client,  "OK");
}



bool Rsp::bp_remove(int socket_client, char* data, size_t len)
{
  enum mp_type type;
  uint32_t addr;
  uint32_t ppc;
  int bp_len;
  Target_core *core;

  core = this->top->target->get_thread(thread_sel);

  if (3 != sscanf(data, "z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    top->log->print(LOG_ERROR, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    top->log->print(LOG_ERROR, "Not a memory bp\n");
    return false;
  }

  top->bkp->remove(addr);

  return this->send_str(socket_client,  "OK");
}



#if 0

Rsp::Rsp(int socket_port, MemIF* mem, LogIF *log, std::list<DbgIF*> list_dbgif, std::list<DbgIF*> list_dbg_cluster_ifs, BreakPoints* bp, DbgIF *main_if) {
  socket_port = socket_port;
  m_mem = mem;
  m_dbgifs = list_dbgif;
  m_dbg_cluster_ifs = list_dbg_cluster_ifs;
  m_bp = bp;
  this->log = log;

  // select one dbg if at random
  if (m_dbgifs.size() == 0) {
    top->log->print(LOG_ERROR, "No debug interface available! Exiting now\n");
    exit(1);
  }

  if (main_if == NULL) main_if = m_dbgifs.front();

  m_thread_init = main_if->get_thread_id();
  thread_sel = m_thread_init;
}

void
Rsp::close() {
  m_bp->clear();
  ::close(socket_in);
}

void
Rsp::resumeCoresPrepare(DbgIF *dbgif, bool step)
{
  top->log->print(LOG_DEBUG, "Preparing core to resume (step: %d)\n", step);

  // now let's handle software breakpoints
  uint32_t ppc;
  dbgif->read_ppc(&ppc);

  // if there is a breakpoint at this address, let's remove it and single-step over it
  bool hasStepped = false;

  if (m_bp->at_addr(ppc)) {

    top->log->print(LOG_DEBUG, "Core is stopped on a breakpoint, stepping to go over (addr: 0x%x)\n", ppc);

    m_bp->disable(ppc);
    dbgif->write(DBG_NPC_REG, ppc); // re-execute this instruction
    dbgif->write(DBG_CTRL_REG, 0x1); // single-step
    while (1) {
      uint32_t value;
      dbgif->read(DBG_CTRL_REG, &value);
      if ((value >> 16) & 1) break;
    }
    m_bp->enable(ppc);
    hasStepped = true;
  }

  dbgif->set_step_mode(step && !hasStepped);
}

void
Rsp::resumeCores() {
  for (std::list<DbgIF*>::iterator it = m_dbg_cluster_ifs.begin(); it != m_dbg_cluster_ifs.end(); it++) {
    DbgIF_cluster *cluster = (DbgIF_cluster *)*it;
    cluster->resume();
  }
}

DbgIF*
Rsp::get_dbgif(int thread_id) {
  for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
    if ((*it)->get_thread_id() == thread_id)
      return *it;
  }

  return NULL;
}

#endif