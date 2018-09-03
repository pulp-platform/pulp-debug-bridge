#ifndef _MINI_EV_H
#define _MINI_EV_H

#include <vector>
#include <functional>
#include <queue>

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
  #define LST_SHUT_RDWR SD_SEND
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
  #define LST_SHUT_RDWR SHUT_WR
#endif

class EventHandle {
    public:
        EventHandle(int priority);
        virtual ~EventHandle();
        int get_priority(); 
};

struct CompareEventHandlePriority
{
    bool operator()(EventHandle* lhs, EventHandle* rhs)
    {
        return lhs->get_priority() < rhs->get_priority();
    }
};

enum SocketEvents 
{
    EventsNone = 0,
    EventsRead = 1,
    EventsWrite = 2
};

class SocketEventHandle : public EventHandle {
    public:
        SocketEventHandle(socket_t socket);
        typedef std::function<void(SocketEventHandle handle, SocketEvents file_events)> socket_event_cb_t;
        SocketEventHandle& set_callback(socket_event_cb_t callback) { this->callback = callback; }
        SocketEventHandle& set_socket_events(SocketEvents events);
        SocketEvents get_socket_events() { return current_events; }
    private:
        socket_t socket = INVALID_SOCKET;
        socket_event_cb_t callback;
        SocketEvents current_events = EventsNone;
};

class PeriodicEventHandle : public EventHandle {

};

class AsyncEventHandle : public EventHandle {

};

class EventLoop {
    public:
        SocketEventHandle& get_socket_event(socket_t socket);
        PeriodicEventHandle& get_periodic_event();
    private:
        std::vector<SocketEventHandle> active_sockets;
        std::queue<EventHandle *, std::vector<EventHandl*e>, active_queue;
};

#endif