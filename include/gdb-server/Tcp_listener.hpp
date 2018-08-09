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
 * Authors: Martin Croome, GreenWaves Technologies (martin.croome@greenwaves-technologies.com)
 */

#ifndef __TCP_LISTENER_H__
#define __TCP_LISTENER_H__

#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501
  #endif
  #include <winsock2.h>
  #include <Ws2tcpip.h>
  #include <Windef.h>
  #include <windows.h>
  typedef int port_t;
  typedef SOCKET socket_t;
  typedef int func_ret_t;
  #define LST_SHUT_RDWR SD_BOTH
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  typedef int port_t;
  typedef int socket_t;
  #define INVALID_SOCKET -1
  typedef int func_ret_t;
  #define SOCKET_ERROR -1
  #define LST_SHUT_RDWR SHUT_RDWR
#endif

#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include "cables/log.h"

#include <functional>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>

class Tcp_socket_owner;

class Tcp_socket {
  public:
    typedef std::function<void()> finished_cb_t;
    typedef std::shared_ptr<Tcp_socket> tcp_socket_ptr_t;
    typedef std::function<void(tcp_socket_ptr_t)> socket_cb_t;

    Tcp_socket(Tcp_socket_owner *owner, socket_t socket);
    func_ret_t receive(void * buf, size_t len, int ms, bool await_all, int flags=0);
    func_ret_t receive(void * buf, size_t len, int flags=0);
    func_ret_t send(void * buf, size_t len, int ms, int flags=0);
    func_ret_t send(void * buf, size_t len, int flags=0);
    void close();
    void shutdown();
    void set_finished_cb(finished_cb_t finished_cb);
  private:
    func_ret_t recvsend(bool send, void * buf, size_t buf_len, size_t cnt, int flags, int ms);
    func_ret_t recvsend_block(bool send, void * buf, size_t len, int flags);
    ssize_t check_error(func_ret_t ret);

    Tcp_socket_owner *owner;
    socket_t socket;
    int block_timeout = 100;
    finished_cb_t f_cb;
    bool is_closed = false, is_shutdown = false, is_closing = false;
};

class Tcp_socket_owner {
  public:
    Tcp_socket_owner(Log *log, Tcp_socket::socket_cb_t connected_cb, Tcp_socket::socket_cb_t disconnected_cb);
    virtual ~Tcp_socket_owner() {};
    friend class Tcp_socket;
  protected:
    virtual void client_disconnected(Tcp_socket * socket) = 0;
    bool print_error(const char * err_str);
    static bool socket_init();
    static void socket_deinit();
    Log *log;
    Tcp_socket::socket_cb_t c_cb, d_cb;
    bool set_blocking(int fd, bool blocking);
    bool is_running = false;
  private:
    static int instances;

  #ifdef _WIN32
    WSADATA wsa_data;
  #endif
};

class Tcp_client : public Tcp_socket_owner {
  public:
    Tcp_client(Log * log, Tcp_socket::socket_cb_t connected_cb, Tcp_socket::socket_cb_t disconnected_cb);
    virtual ~Tcp_client() {}
    Tcp_socket::tcp_socket_ptr_t connect(const char * address, int port);
  private:
    Tcp_socket::tcp_socket_ptr_t client;
    void client_disconnected(Tcp_socket * socket);
};

class Tcp_listener : public Tcp_socket_owner {
public:
  Tcp_listener(Log *log, port_t port, Tcp_socket::socket_cb_t connected_cb, Tcp_socket::socket_cb_t disconnected_cb);
  virtual ~Tcp_listener() {}
  bool start();
  void stop();

private:
  void client_disconnected(Tcp_socket * socket);
  void listener_routine();

  port_t port;
  socket_t socket_in;
  bool is_stopping = false;
  Tcp_socket::tcp_socket_ptr_t client;
  std::thread *listener_thread;
};

#endif