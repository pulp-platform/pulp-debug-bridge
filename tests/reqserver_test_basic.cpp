
#include <memory>
#include <thread>

#include "mock_cable.hpp"
#include "reqserver.hpp"
#include "debug_bridge/reqserver.h"
#include "events/events.hpp"

void client()
{
    Log log_("test");
    // Log::log_level = 1;
    auto el = EventLoop::getLoop();

    // always use shared pointer for socket owners!
    auto c = std::make_shared<Tcp_client>(&log_, el);

    int trans_id = 1;
    int *ptrans_id = &trans_id;

    c->on_connected(
        [ptrans_id](tcp_socket_ptr_t sock){
            sock->on_write([ptrans_id](const tcp_socket_ptr_t& sock, circular_buffer_ptr_t buf){
                std::cout << "# Writing one\n";
                reqserver_req_t req;
                req.trans_id = (*ptrans_id)++;
                req.addr = 1000;
                req.len = 100;
                // sock->set_events(Readable);
                buf->write_copy(&req, sizeof(req));
            });

            sock->on_read([](const tcp_socket_ptr_t& sock, circular_buffer_ptr_t buf){
                std::cout << "# Read one\n";
                reqserver_rsp_payload_t rsp;
                buf->read_copy(&rsp, sizeof(rsp));
                sock->set_events(Both);
            });

            sock->once_closed([](){
                std::cout << "# Writing socket signals closing\n";
            });

            std::cout << "# Enable write events\n";
            // enable writing on the socket
            sock->set_events(Both);
        }
    );

    c->on_disconnected(
        [](tcp_socket_ptr_t sock){
            std::cout << "# Client signals socket closed\n";
        }
    );
    c->connect("127.0.0.1", 9999);
    el->start();
}

int main( int argc, const char* argv[] )
{

    auto el = EventLoop::getLoop();
    auto cable = std::make_shared<MockCable>();

    auto req_srv = std::make_shared<ReqServer>(el, cable, 9999);
    req_srv->start();
    std::thread client_thread(client);
    el->start();
    client_thread.join();
    std::cout << "Test Passed\n";
}
