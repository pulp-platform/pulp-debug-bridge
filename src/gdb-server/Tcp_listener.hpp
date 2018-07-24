#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501
  #endif
  #include <winsock2.h>
  #include <Ws2tcpip.h>
  typedef int port_t;
  typedef SOCKET socket_t;
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
#endif

#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include "cables/log.h"

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

class Tcp_listener {
public:
  class Exception: public std::exception
  {
  public:
      explicit Exception(const char* message):
        msg_(message)
        {
        }
      explicit Exception(const std::string& message):
        msg_(message)
        {}
      virtual ~Exception() throw (){}
      virtual const char* what() const throw (){
        return msg_.c_str();
      }

  protected:
      std::string msg_;
  };

  class Fatal_exception: public Exception
  { };

  typedef std::function<void()> finished_cb_t;

  class Tcp_socket {
  public:
    Tcp_socket(Tcp_listener *listener, socket_t socket);
    func_ret_t receive(void * buf, size_t len, int ms, bool await_all, int flags=0);
    func_ret_t receive(void * buf, size_t len, bool await_all, int flags=0);
    func_ret_t send(void * buf, size_t len, int ms, int flags=0);
    func_ret_t send(void * buf, size_t len, int flags=0);
    void close();
    void set_finished_cb(finished_cb_t finished_cb);
  private:
    func_ret_t recvsend(bool send, void * buf, size_t buf_len, size_t cnt, int flags, int ms);
    func_ret_t recvsend_block(bool send, void * buf, size_t len, int flags);
    ssize_t check_error(func_ret_t ret);
    socket_t socket;
    Tcp_listener *listener;
    int block_timeout = 100;
    finished_cb_t f_cb = nullptr;
    bool is_closed = false;
  };

  typedef std::function<void(Tcp_listener::Tcp_socket *)> socket_cb_t;

  Tcp_listener(Log *log, port_t port, socket_cb_t connected_cb, socket_cb_t disconnected_cb);
  bool start();
  bool stop();

private:
  void client_disconnected();
  void listener_routine();
  int socket_init();
  int socket_deinit();
  bool set_blocking(int fd, bool blocking);

  Log *log;
  port_t port;
  socket_cb_t c_cb, d_cb;
  socket_t socket_in;
  bool running = false;
  Tcp_socket *client;
  std::thread *listener_thread;
};

