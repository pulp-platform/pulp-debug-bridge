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
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "cables/log.h"
#include "cables/adv_dbg_itf/adv_dbg_itf.hpp"
#include "debug_bridge/proxy.hpp"
#include "cable.hpp"
#include "gdb-server/Tcp_listener.hpp"

class Jtag_proxy : public Cable {
  public:

    Jtag_proxy(Log* log);
    ~Jtag_proxy() = default;
    
    bool connect(js::config *config);

    bool bit_inout(char* inbit, char outbit, bool last);

    bool stream_inout(char* instream, char* outstream, unsigned int n_bits, bool last);

    bool jtag_reset(bool active);

    int flush();

    bool chip_reset(bool active);

  private:

    Tcp_client * m_client;
    Tcp_socket::tcp_socket_ptr_t m_socket;
    int m_port = 0;
    const char *m_server;
    int timeout;

    void client_connected(Tcp_socket::tcp_socket_ptr_t);
    void client_disconnected(Tcp_socket::tcp_socket_ptr_t);

    bool proxy_stream(char* instream, char* outstream, unsigned int n_bits, bool last, int bit);
    Log * log;

};