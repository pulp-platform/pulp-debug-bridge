#include "events/events.hpp"
#include "events/tcp-events.hpp"
#include "log/log.hpp"

#include <iostream>
#include <memory>

int main( int argc, const char* argv[] )
{
    Log log_("test");
    Log::log_level = 1;

    auto el = EventLoop::getLoop();
    // always use shared pointer for socket owners!
    auto l = std::make_shared<Tcp_listener>(&log_, el, 10000);
    l->set_connected_cb(
        [](const Tcp_socket::tcp_socket_ptr_t& sock){
            sock->set_read_cb([](const Tcp_socket::tcp_socket_ptr_t& UNUSED(sock), Tcp_socket::circular_buffer_ptr_t buf){
                char cbuf[1024];
                size_t clen = buf->read_copy(cbuf, 1024);
                cbuf[clen] = 0;
                printf("# %s\n", cbuf);
            });
            sock->set_closed_cb([](){
                std::cout << "# Reading socket signals closing\n";
            });
            sock->set_events(Readable);
        }
    );
    l->set_disconnected_cb(
        [&](Tcp_socket::tcp_socket_ptr_t sock){
            std::cout << "# Listener signals socket closed\n";
            l->stop();
        }
    );
    l->set_state_cb(
        [](listener_state_t state) {
            std::cout << "# Listener state: " << (state == ListenerStarted?"started":"stopped") << "\n";  
        }
    );
    l->start();

    int pkt_cnt = 8;
    int *ppkt_cnt = &pkt_cnt;

    // always use shared pointer for socket owners!
    auto c = std::make_shared<Tcp_client>(&log_, el);

    c->set_connected_cb(
        [ppkt_cnt](Tcp_socket::tcp_socket_ptr_t sock){
            sock->set_write_cb([ppkt_cnt](const Tcp_socket::tcp_socket_ptr_t& sock, Tcp_socket::circular_buffer_ptr_t buf){
                std::cout << "# Write packet\n";

                const char *cbuf = "testing";
                #ifndef NDEBUG
                assert(buf->write_copy(cbuf, 8) == 8);
                #else
                buf->write_copy(cbuf, 8);
                #endif
                if (((*ppkt_cnt)--)<=0) {
                    // sock->set_events(None);
                    sock->close();
                }
            });

            sock->set_closed_cb([](){
                std::cout << "# Writing socket signals closing\n";
            });

            // enable writing on the socket
            sock->set_events(Writable);
        }
    );

    c->set_disconnected_cb(
        [](Tcp_socket::tcp_socket_ptr_t sock){
            std::cout << "# Client signals socket closed\n";
        }
    );
    c->connect("127.0.0.1", 10000);
    el->start();
    std::cout << "# Loop exited\n";
    el = nullptr;
    c = nullptr;
    l = nullptr;
    std::cout << "Test Passed\n";

    return 0;
}