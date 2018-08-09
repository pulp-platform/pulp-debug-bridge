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

#include "jtag-proxy.hpp"

Jtag_proxy::Jtag_proxy(Log* log) : log(log)
{
  m_client = new Tcp_client(
    log,
    [this](Tcp_socket::tcp_socket_ptr_t client) { return this->client_connected(client); }, 
    [this](Tcp_socket::tcp_socket_ptr_t client) { return this->client_disconnected(client); }
  );
}   

void Jtag_proxy::client_connected(Tcp_socket::tcp_socket_ptr_t)
{
  log->user("JTAG Proxy: Connected to (%s:%d)\n", m_server, m_port);
}

void Jtag_proxy::client_disconnected(Tcp_socket::tcp_socket_ptr_t)
{
  log->user("JTAG Proxy: Disconnected from (%s:%d)\n", m_server, m_port);
}

bool Jtag_proxy::connect(js::config *config)
{
  js::config *proxy_config = config->get("jtag-proxy");

  if (proxy_config == NULL || proxy_config->get("port") == NULL)
  {
    log->error("Didn't find any information for JTAG proxy\n");
    return false;
  }

  m_port = proxy_config->get("port")->get_int();

  if (proxy_config->get("host") == NULL)
    m_server = "localhost";
  else
    m_server = proxy_config->get("host")->get_str().c_str();

  log->user("JTAG Proxy: Connecting to (%s:%d)\n", m_server, m_port);

  if ((m_socket = m_client->connect(m_server, m_port)) == nullptr) {
    log->error("Unable to connect to %s port %d\n", m_server, m_port);
    return false;
  }
  return true;
}

bool Jtag_proxy::bit_inout(char* inbit, char outbit, bool last)
{
  return stream_inout(inbit, &outbit, 1, last);
}

bool Jtag_proxy::proxy_stream(char* instream, char* outstream, unsigned int n_bits, bool last, int bit)
{
  proxy_req_t req;
  req.type=DEBUG_BRIDGE_JTAG_REQ;
  req.jtag.bits = n_bits;
  req.jtag.tdo = instream != NULL;

  if (n_bits >= (1<<16)) return false;

  std::vector<uint8_t> buffer(n_bits, 0);
  uint8_t value = 0;
  if (outstream)
  {
    for (size_t i=0; i<n_bits; i++)
    {

      if ((i % 8) == 0)
      {
        value = *(unsigned char *)outstream;
        outstream++;
      }

      buffer[i] = (value & 1) << bit;
      if (bit != DEBUG_BRIDGE_JTAG_TRST) buffer[i] |= 1 << DEBUG_BRIDGE_JTAG_TRST;

      value >>= 1;
    }
  }

  if (last)
  {
    buffer[n_bits-1] |= 1 << DEBUG_BRIDGE_JTAG_TMS;
  }

  m_socket->send((void *)&req, sizeof(req));
  m_socket->send((void *)(&(buffer[0])), n_bits);
  if (instream != NULL)
  {

    ::memset((void *)instream, 0, (n_bits + 7) / 8);
    if (m_socket->receive((void *)instream, (n_bits + 7) / 8, 0) != (func_ret_t)((n_bits + 7) / 8))
      return false;
  }
  return true;
}

bool Jtag_proxy::stream_inout(char* instream, char* outstream, unsigned int n_bits, bool last)
{
  return proxy_stream(instream, outstream, n_bits, last, DEBUG_BRIDGE_JTAG_TDI);
}

bool Jtag_proxy::jtag_reset(bool active)
{
  int value = !active;
  return proxy_stream(NULL, (char *)&value, 1, 0, DEBUG_BRIDGE_JTAG_TRST);
}

int Jtag_proxy::flush()
{
  return true;
}

bool Jtag_proxy::chip_reset(bool active)
{
  proxy_req_t req;
  req.type=DEBUG_BRIDGE_RESET_REQ;
  req.reset.active = active;
  m_socket->send((void *)&req, sizeof(req));
  return true;
}
