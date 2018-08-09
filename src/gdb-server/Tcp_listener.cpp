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

#include "Tcp_listener.hpp"

Tcp_socket_owner::Tcp_socket_owner(Log *log, Tcp_socket::socket_cb_t connected_cb, Tcp_socket::socket_cb_t disconnected_cb) 
  : log(log), c_cb(connected_cb), d_cb(disconnected_cb) 
{
  log->debug("Tcp_socket_owner constructor - conn_cb: %s disconn_cb: %s\n", (c_cb==NULL?"no":"yes"), (d_cb==NULL?"no":"yes"));
}

bool Tcp_socket_owner::set_blocking(int fd, bool blocking)
{
  if (fd < 0) {
    return false;
  }

#ifdef _WIN32
  unsigned long mode = blocking ? 0 : 1;
  return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? true : false;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }

  flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

  return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
#endif
}

int Tcp_socket_owner::instances = 0;

bool Tcp_socket_owner::socket_init()
{
  instances++;
  if (instances == 1) {
    #ifdef _WIN32
      return WSAStartup(MAKEWORD(1,1), &wsa_data) == 0;
    #else
      return true;
    #endif
  } else {
    return true;
  }
}

bool Tcp_socket_owner::print_error(const char * err_str)
{
#ifdef _WIN32
  int err_num;
  if ((err_num = WSAGetLastError()) != WSAEWOULDBLOCK) {
    log->error(err_str, err_num);
    return true;
  }
#else
  if (errno != EWOULDBLOCK) {
    log->error(err_str, errno);
    return true;
  }
#endif
  return false;
}

void Tcp_socket_owner::socket_deinit()
{
  instances--;
  if (instances == 0) {
    #ifdef _WIN32
      WSACleanup();
    #endif
  }
}

Tcp_client::Tcp_client(Log * log, Tcp_socket::socket_cb_t connected_cb, Tcp_socket::socket_cb_t disconnected_cb)
  : Tcp_socket_owner(log, connected_cb, disconnected_cb)
{

}

void Tcp_client::client_disconnected(Tcp_socket *)
{
  if (client) {
    if (d_cb) d_cb(client);
    client = NULL;
  }
}

Tcp_socket::tcp_socket_ptr_t Tcp_client::connect(const char * address, int port)
{
  struct sockaddr_in addr;
  struct hostent *he;
  socket_t socket_cl;

  socket_cl = socket(PF_INET, SOCK_STREAM, 0);
  if(socket_cl == INVALID_SOCKET)
  {
    print_error("unable to create socket - error %d\n");
    return Tcp_socket::tcp_socket_ptr_t(nullptr);
  }

  if((he = gethostbyname(address)) == NULL) {
    print_error("unable to find host - error %d\n");
    return Tcp_socket::tcp_socket_ptr_t(nullptr);
  }

  log->user("Connecting to (%s:%d)\n", address, port);

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  if(::connect(socket_cl, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    print_error("unable to connect - error %d\n");
    return Tcp_socket::tcp_socket_ptr_t(nullptr);
  }
  log->user("Connected to (%s:%d)\n", address, port);
  set_blocking(socket_cl, false);
  is_running = true;
  client = std::make_shared<Tcp_socket>(this, socket_cl);
  if (c_cb) c_cb(client);
  return client;
}

Tcp_listener::Tcp_listener(Log *log, port_t port, Tcp_socket::socket_cb_t connected_cb, Tcp_socket::socket_cb_t disconnected_cb)
  : Tcp_socket_owner(log, connected_cb, disconnected_cb), port(port)
{
  log->debug("create listener conn_cb: %s disconn_cb: %s\n", (connected_cb==NULL?"no":"yes"), (disconnected_cb==NULL?"no":"yes"));
}

void Tcp_listener::listener_routine()
{
  while(is_running)
  {
    socket_t socket_client;
    func_ret_t ret;

    fd_set set;
    struct timeval tv;

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&set);
    FD_SET(socket_in, &set);

    ret = select(socket_in+1, &set, NULL, NULL, &tv);

    if (ret > 0) {
      if((socket_client = accept(socket_in, NULL, NULL)) == INVALID_SOCKET)
      {
        if (!print_error("Tcp_listener: Unable to accept connection: %d\n"))
          continue;

        close(socket_client);
        continue;
      }

      log->user("Tcp_listener: Client connected!\n");

      set_blocking(socket_client, false);
      client = std::make_shared<Tcp_socket>(this, socket_client);

      if (c_cb) {
        log->debug("Tcp_listener: call connected callback\n");
        c_cb(client);
      } else {
        log->debug("Tcp_listener: no connected callback - closing socket\n");
      }
      log->user("Tcp_listener: client finished\n");
    } else if (ret == SOCKET_ERROR) {
      if (is_running) {
        print_error("Tcp_listener: error on listening socket: %d\n");
      }
      break;
    }
  }
  log->debug("listener thread finished\n");
}

void Tcp_listener::client_disconnected(Tcp_socket *)
{
  if (client) {
    if (d_cb) d_cb(client);
    client = NULL;
  }
}

void Tcp_listener::stop()
{
  if (this->is_stopping) return;
  this->is_stopping = true;
  log->debug("Tcp_listener stopped (running %d)\n", this->is_running);
  if (this->is_running) {
    if (client) {
      client->close();
    }
    this->is_running = false;
    ::close(socket_in);
    listener_thread->join();
  }
  Tcp_socket_owner::socket_deinit();
  this->is_stopping = false;
}

bool Tcp_listener::start()
{
  if (is_running) {
    return true;
  }
  struct sockaddr_in addr;
  int yes = 1;

  log->debug("Tcp_listener started (running %d)\n", this->is_running);

  Tcp_socket_owner::socket_init();
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  socket_in = socket(PF_INET, SOCK_STREAM, 0);
  if(socket_in == INVALID_SOCKET)
  {
    print_error("Unable to create comm socket: %d\n");
    return false;
  }

  if(setsockopt(socket_in, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    print_error("Unable to setsockopt on the socket: %d\n");
    return false;
  }

  if(bind(socket_in, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    print_error("Unable to bind the socket: %d\n");
    return false;
  }

  if(listen(socket_in, 1) == -1) {
    print_error("Unable to listen: %d\n");
    return false;
  }

  this->is_running = true;
  this->is_stopping = false;
  listener_thread = new std::thread(&Tcp_listener::listener_routine, this);

  log->user("RSP server opened on port %d\n", port);

  return is_running;
}

Tcp_socket::Tcp_socket(Tcp_socket_owner *owner, socket_t socket) : owner(owner), socket(socket)
{
}

ssize_t Tcp_socket::check_error(func_ret_t)
{
#ifdef _WIN32
  int err_num;
  if ((err_num = WSAGetLastError()) != WSAEWOULDBLOCK) {
    listener->log->error("Error on client socket %d - closing\n", err_num);
#else
  if (errno != EWOULDBLOCK) {
    owner->log->error("Error on client socket %d - closing\n", errno);
#endif
    this->close();
    return -1;
  } else {
    return 0;
  }
}

void Tcp_socket::shutdown()
{
  owner->log->debug("Shutdown client socket\n");
  fd_set rfds;
  struct timeval tv;
  char buf[100];

  if (is_shutdown) return;
  is_shutdown = true;

  if (::shutdown(socket, LST_SHUT_RDWR) == -1) return;

  tv.tv_sec = 0;
  tv.tv_usec = 500 * 1000;

  FD_ZERO(&rfds);
  FD_SET(socket, &rfds);

  while(1) {
    func_ret_t ret;
  #ifdef _WIN32
    ret = select(0, &rfds, NULL, NULL, &tv);
  #else
    ret = select(socket+1, &rfds, NULL, NULL, &tv);
  #endif
    if (ret > 0) {
      ret = recv(socket, buf, 100, 0);
      if (ret == SOCKET_ERROR||ret == 0) {
        break;
      }
    } else {
      break;
    }
  }
  owner->log->debug("Shutdown finished waiting\n");
}


void Tcp_socket::close()
{
  if (!is_closed) {
    if (is_closing) return;
    is_closing = true;
    owner->log->debug("Close client socket %d\n", is_shutdown);
    if (!is_shutdown) {
      this->shutdown();
    }
    owner->log->debug("Close client socket\n");
    is_closed = true;
    // clear blocking on the socket so that if linger is set we wait
    owner->set_blocking(socket, false);
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
    owner->client_disconnected(this);
    this->is_closing = false;
  }
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

#define timercmp(tvp, uvp, cmp) \
        ((tvp)->tv_sec cmp (uvp)->tv_sec || \
         (tvp)->tv_sec == (uvp)->tv_sec && (tvp)->tv_usec cmp (uvp)->tv_usec)
#endif

// recv/send buf which must be buf_cnt in size. If cnt < 0 then send/recv as much as possible. 
// If cnt > 0 send/recv exactly that amount
func_ret_t Tcp_socket::recvsend(bool send, void * buf, size_t buf_len, size_t cnt, int flags, int ms)
{
  if (is_closed) {
    return -1;
  }
  func_ret_t res = 0;
  fd_set rfds, wfds;
  struct timeval tv, now;

  assert(cnt<=buf_len);

  ::gettimeofday(&now, NULL);

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  if (send) {
    FD_SET(socket, &wfds);
  } else {
    FD_SET(socket, &rfds);
  }

  tv.tv_sec = (ms * 1000) / 1000000;
  tv.tv_usec = (ms * 1000) % 1000000;

  while (1) {
    func_ret_t ret;

    if (!owner->is_running) {
      this->close();
      return SOCKET_ERROR;
    }

    ret = select(socket+1, &rfds, &wfds, NULL, &tv);

    if (!owner->is_running) {
      this->close();
      return SOCKET_ERROR;
    }

    if (ret > 0) {
      if (send) {
        ret = ::send(socket, buf, cnt, flags);
      } else {
        ret = recv(socket, buf, cnt<=0?buf_len:cnt, flags);
      }

      if (!owner->is_running) {
        this->close();
        return SOCKET_ERROR;
      }

      // check if the connection has closed
      if (!send && ret == 0) {
        this->is_shutdown = true;
        this->close();
        res = SOCKET_ERROR;
        break;
      }

      // check it there is an error
      if (ret == SOCKET_ERROR) {
        // ret will be 0 if the socket would block
        if ((ret = this->check_error(ret)) != 0) {
          this->is_shutdown = true;
          res = ret;
          break;
        };
      }
      
      res += ret;

      // check if we should try for more characters
      if (cnt>0 && (size_t) res < cnt) {
        struct timeval new_now, used;
        gettimeofday(&new_now, NULL);
        timersub(&new_now, &now, &used);
        if (timercmp(&tv, &used, <)) {
          // no more time - return what we have
          break;
        }
        timersub(&tv, &used, &tv);
        buf = (void *)((char *) buf + res);
        cnt -= ret;
      } else {
        // We're waiting for anything so just return what we got
        break;
      }

    } else if (ret == SOCKET_ERROR) {
      res = this->check_error(ret);
      this->is_shutdown = true;
      break;
    } else {
      res = 0;
      break;
    }
  }
  return res;
}

func_ret_t Tcp_socket::recvsend_block(bool send, void * buf, size_t len, int flags)
{
  func_ret_t ret = 0;
  while (ret == 0) {
    ret = this->recvsend(send, buf, len, len, flags, this->block_timeout);
  }
  return ret;
}

func_ret_t Tcp_socket::receive(void * buf, size_t len, int ms, bool await_all, int flags)
{
  return this->recvsend(false, buf, len, await_all?len:0, flags, ms);
}

func_ret_t Tcp_socket::receive(void * buf, size_t len, int flags)
{
  return this->recvsend_block(false, buf, len, flags);
}

func_ret_t Tcp_socket::send(void * buf, size_t len, int ms, int flags)
{
  return this->recvsend(true, buf, len, len, flags, ms);
}

func_ret_t Tcp_socket::send(void * buf, size_t len, int flags)
{
  return this->recvsend_block(true, buf, len, flags);
}
