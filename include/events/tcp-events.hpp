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

#ifndef __TCP_EVENTS_H__
#define __TCP_EVENTS_H__

#include "platform.h"

#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include "events.hpp"
#include "log/log.hpp"
#include "circular-buffer.hpp"

#include <functional>
#include <thread>
#include <mutex>
#include <memory>
#include <map>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <string>


class TcpException: public std::exception {
public:
   TcpException(const std::string& msg) : msg(msg) {}
  ~TcpException() {}

   std::string getMessage() const {return(msg);}
private:
   std::string msg;
};

class Tcp_socket_owner;

enum TcpSocketState {
  SocketClosed,
  SocketOpen,
  SocketShuttingDown,
  SocketShutDown
};

class Tcp_socket : public std::enable_shared_from_this<Tcp_socket> {
  public:
    typedef std::shared_ptr<CircularCharBuffer> circular_buffer_ptr_t;
    typedef std::shared_ptr<Tcp_socket> tcp_socket_ptr_t;
    typedef std::function<void(const tcp_socket_ptr_t&)> sock_state_cb_t;
    typedef std::function<void()> state_cb_t;
    typedef std::function<void(int error)> error_cb_t;
    typedef std::function<void(const tcp_socket_ptr_t&, const circular_buffer_ptr_t&)> data_cb_t;

    static const char * socket_state_str(TcpSocketState state);

    Tcp_socket(std::shared_ptr<Tcp_socket_owner> owner, socket_t socket, size_t read_size=DEFAULT_BUFFER_SIZE, size_t write_size=DEFAULT_BUFFER_SIZE);
    ~Tcp_socket();

    void set_read_cb(data_cb_t cb) { read_cb = cb; };
    void set_write_cb(data_cb_t cb) { write_cb = cb; };
    void set_closed_cb(state_cb_t cb) { closed_cb = cb; };
    void set_error_cb(error_cb_t cb) { error_cb = cb; };

    void read_buffer(const data_cb_t& cb);
    void write_buffer(const data_cb_t& cb);

    ssize_t read_immediate(void * buf, size_t len, bool read_all=false);
    ssize_t write_immediate(const void * buf, size_t len, bool write_all=false);

    bool set_blocking(bool blocking);

    void set_events(FileEvents events);

    socket_t get_handle() { return socket; }
    void close();
    void close_immediate();
    void shutdown();
  private:
    bool check_error(int *plat_err_no);
    bool socket_writable();
    bool socket_readable();
    bool socket_timeout();
    void socket_shutdown();
    bool after_read();
    bool after_write();

    EventLoop::SpFileEvent ev_socket;
    EventLoop::SpTimerEvent ev_socket_timeout;
    std::shared_ptr<Tcp_socket_owner> owner;
    socket_t socket;
    circular_buffer_ptr_t in_buffer;
    circular_buffer_ptr_t out_buffer;
    FileEvents user_events = None, socket_events = None;   
    bool write_flowing = true, read_flowing = true;
    size_t high, low;
    data_cb_t read_cb;
    data_cb_t write_cb;
    state_cb_t closed_cb;
    error_cb_t error_cb;
    TcpSocketState state = SocketOpen;
};


class Tcp_socket_owner : public std::enable_shared_from_this<Tcp_socket_owner> {
  public:
    Tcp_socket_owner(Log *log, EventLoop::SpEventLoop el);
    virtual ~Tcp_socket_owner();
    friend class Tcp_socket;
    void set_connected_cb(Tcp_socket::sock_state_cb_t conn_cb) { this->conn_cb = conn_cb; }
    void set_disconnected_cb(Tcp_socket::sock_state_cb_t disco_cb) { this->disco_cb = disco_cb; }
  protected:
    virtual void client_disconnected(socket_t socket);
    virtual Tcp_socket::tcp_socket_ptr_t client_connected(socket_t socket);
    void close_all();
    size_t socket_count() { return owned_sockets.size(); }
    bool print_error(const char * err_str);
    bool socket_init();
    void socket_deinit();
    Log *log;
    EventLoop::SpEventLoop el;
    Tcp_socket::sock_state_cb_t conn_cb, disco_cb;
    EventLoop::SpTimerEvent ev_timer;
    std::map<socket_t, Tcp_socket::tcp_socket_ptr_t> owned_sockets;
    bool is_running = false;
  private:
  #ifdef _WIN32
    WSADATA m_wsa_data;
  #endif
};

class Tcp_client : public Tcp_socket_owner {
  public:
    Tcp_client(Log * log, EventLoop::SpEventLoop el);
    virtual ~Tcp_client();
    void connect(const char * address, int port, int timeout_ms=5000);
    Tcp_socket::tcp_socket_ptr_t connect_blocking(const char * address, int port, int timeout_ms=5000);
  private:
    socket_t prepare_socket(socket_t fd, const char * address, int port, struct sockaddr_in *addr);
    socket_t connecting_socket = INVALID_SOCKET;
    EventLoop::SpFileEvent ev_connect;
    EventLoop::SpTimerEvent ev_connect_timout;
    char connecting_address[512];
    int connecting_port;
};

typedef enum {
  ListenerStopped = 0,
  ListenerStarted = 1
} listener_state_t;

class Tcp_listener : public Tcp_socket_owner {
public:
  typedef std::function<void(listener_state_t)> listener_state_cb_t;
  Tcp_listener(Log *log, EventLoop::SpEventLoop el, port_t port);
  virtual ~Tcp_listener() {}
  void start();
  void stop();
  void set_state_cb(listener_state_cb_t state_cb) { this->state_cb = state_cb; }
  void setAccepting(bool accepting) { this->accepting = accepting; }

private:
  void listener_state_change(listener_state_t state);
  void listener_routine();
  void accept_socket();

  port_t port;
  socket_t socket_in;
  bool is_stopping = false;
  std::thread *listener_thread;
  listener_state_cb_t state_cb;
  bool accepting;
  EventLoop::SpFileEvent ev_incoming;
};

#endif