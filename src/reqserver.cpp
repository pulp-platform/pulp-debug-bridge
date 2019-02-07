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

#include <algorithm>

#include "reqserver.hpp"

// Rsp

ReqServer::ReqServer(const EventLoop::SpEventLoop &event_loop, std::shared_ptr<Cable> cable, int port) :
  m_event_loop(std::move(event_loop)), m_cable(std::move(cable)), m_port(port), log("REQS")
{
}

void ReqServer::client_connected(const tcp_socket_ptr_t &client)
{
  log.user("client connected\n");
  if (m_client) {
    log.error("already connected: disconnecting\n");
    client->close();
    return;
  }
  m_client = std::make_shared<ReqServer::Client>(this->shared_from_this(), client);
}

void ReqServer::client_disconnected(const tcp_socket_ptr_t &UNUSED(sock)) {
  log.user("client disconnected\n");
  if (m_stopping) {
    stop_listener();
  }
  m_client = nullptr;
}

void ReqServer::stop()
{
  if (!m_started||m_stopping) return;
  m_stopping = true;
  if (m_client) {
    m_client->stop();
    return;
  }
  stop_listener();
}

void ReqServer::stop_listener()
{
  if (m_listener) {
    m_listener->stop();
    m_listener = nullptr;
  }
  m_stopping = false;
  m_started = false;
}

void ReqServer::start() {
  m_started = true;
  m_listener = std::make_shared<Tcp_listener>(
    &log,
    m_event_loop,
    m_port);

  using namespace std::placeholders;

  m_listener->on_connected(std::bind(&ReqServer::client_connected, this, _1));
  m_listener->on_disconnected(std::bind(&ReqServer::client_disconnected, this, _1));

  m_listener->on_state_change([this](listener_state_t state) {
      log.debug("Listener status %d\n", state);
    }
  );
  m_listener->start();
}

void ReqServer::target_reset() {
  if (!m_client) return;
  m_client->target_reset();
}

void ReqServer::target_alert() {
  if (!m_client) return;
  m_client->target_alert();
}


// Rsp::Client

ReqServer::Client::Client(const std::shared_ptr<ReqServer> reqserver, tcp_socket_ptr_t client) : m_reqserver(std::move(reqserver)), m_client(std::move(client)), log("RSPC")
{
  m_trans_te = m_reqserver->m_event_loop->getTimerEvent(std::bind(&ReqServer::Client::process_transaction, this));
  m_pkt_to_te = m_reqserver->m_event_loop->getTimerEvent(std::bind(&ReqServer::Client::packet_timeout, this));

  // socket -> packet codec
  m_client->on_read([this](const tcp_socket_ptr_t& UNUSED(sock), circular_buffer_ptr_t buf){
    bool clear_timer;
    if (this->m_cur_req.receive(buf, &clear_timer)) {
      this->m_pending_reqs.push(this->m_cur_req);
      this->m_cur_req.reset();
      this->m_trans_te->setTimeout(0);
    }
    this->m_pkt_to_te->setTimeout((clear_timer?kEventLoopTimerDone:1000000));
  });
  m_client->on_write([this](const tcp_socket_ptr_t& UNUSED(sock), circular_buffer_ptr_t buf){
    if (this->m_completed_reqs.size() == 0) {
      this->m_client->set_events(Readable);
    } else if (this->m_completed_reqs.front().send(buf)) {
      this->m_completed_reqs.pop();
    }
    // if we need to send an alert or reset do it here where we have finished a req
    if (
      // need to send an alert or reset
      (this->m_send_alert||this->m_send_reset) && 
      // pending queue is empty or no transaction in progress
      (this->m_completed_reqs.size() == 0 || !this->m_completed_reqs.front().is_in_progress()) && 
      // have room to send
      (buf->available() >= sizeof(reqserver_rsp_t)))
    {
      reqserver_rsp_t rsp;
      rsp.trans_id = 0;
      if (this->m_send_alert) {
        rsp.type = REQSERVER_ALERT_RSP;
        // alert clears all unsent transactions
        while (!this->m_completed_reqs.empty()) this->m_completed_reqs.pop();
      } else {
        rsp.type = REQSERVER_ERROR_RSP;
      }
      this->m_send_alert = false;
      this->m_send_reset = false;
      buf->write_copy((void *)&rsp, sizeof(reqserver_rsp_t));
    }
  });
  m_client->set_events(Readable);
}

ReqServer::Client::~Client() {
  m_trans_te->setTimeout(kEventLoopTimerDone);
  m_pkt_to_te->setTimeout(kEventLoopTimerDone);
}

void ReqServer::Client::stop()
{
  log.debug("ReqServer client stopping\n");
  m_trans_te->setTimeout(kEventLoopTimerDone);
  m_pkt_to_te->setTimeout(kEventLoopTimerDone);
  m_client->close();
}

int64_t ReqServer::Client::process_transaction() {
  log.debug("Process transaction (size %d)\n", m_pending_reqs.size());
  if (m_pending_reqs.size() == 0) return kEventLoopTimerDone;
  if (m_pending_reqs.front().execute(this->m_reqserver->m_cable)) {
    m_completed_reqs.push(m_pending_reqs.front());
    m_pending_reqs.pop();
    m_client->set_events(Both);
  }
  return 100;
}

int64_t ReqServer::Client::packet_timeout() {
  log.debug("Packet timeout\n");
  m_client->read_buffer([](const tcp_socket_ptr_t& UNUSED(sock), circular_buffer_ptr_t buf){
    buf->reset();
  });
  log.debug("Reset current request\n");
  this->m_cur_req.reset();
  return kEventLoopTimerDone;
}

void ReqServer::Client::target_alert() {
  m_send_alert = true;
  m_client->set_events(Both);
}

void ReqServer::Client::target_reset() {
  m_send_reset = true;
  m_send_alert = false;
  while (!m_pending_reqs.empty()) m_pending_reqs.pop();
  if (!m_completed_reqs.empty() && !m_completed_reqs.front().is_in_progress()) {
    while (!m_completed_reqs.empty()) m_completed_reqs.pop();
  }
  m_client->set_events(Both);
}

void ReqServer::Request::reset() {
  this->m_buf.clear();
  this->m_pos = 0;
  this->m_error = false;
  this->m_in_progress = false;
  memset(&this->m_req, 0, sizeof(this->m_req));
}

bool ReqServer::Request::receive(circular_buffer_ptr_t buf, bool * clear_timer) {
  size_t len;
  void * vbuf;
  while(true) {
    if(this->m_in_progress) {
      // in progress is true when reading data to write. m_pos points to where we got up to
      while(true) {
        buf->read_block(&vbuf, &len);
        if (len > 0) {
          this->m_buf.insert(this->m_buf.end(), (char *)vbuf, (char *)vbuf + len);
          this->m_pos += len;
          buf->commit_read(len);
        } else break;
      }
      if (this->m_pos >= this->m_req.len) {
        *clear_timer = true;
        this->m_in_progress = false;
        this->m_pos = 0;
        return true;
      } else {
        *clear_timer = false;
        return false;
      }
    } else if (buf->size() > sizeof(this->m_req)) {
      // valid size for a buffer
      buf->write_copy(((char *)&this->m_req), sizeof(this->m_req));
      if (this->m_req.type > REQSERVER_MAX_REQ || this->m_req.type < 0 || this->m_req.len <= 0 || this->m_req.len > 5000000) {
        buf->reset();
        this->m_error = true;
        *clear_timer = true;
        return true;
      }
      if (this->m_req.type == REQSERVER_WRITEMEM_REQ) {
        // this is a write request to loop to get the data
        this->m_in_progress = true;
        continue;
      }
      this->m_buf.reserve(static_cast<size_t>(this->m_req.len));
      *clear_timer = true;
      return true;
    } else {
      *clear_timer = buf->size() == 0;
      return false;
    }
  }
}

bool ReqServer::Request::send(circular_buffer_ptr_t buf) {
  while (true) {
    if (this->m_in_progress) {
      this->m_pos += buf->write_copy(&this->m_buf[this->m_pos], this->m_req.len - this->m_pos);
      if (this->m_pos >= this->m_req.len) {
        this->m_in_progress = false;
        return true;
      } else {
        return false;
      }
    } else if (this->m_error) {
      if (buf->available() < sizeof(reqserver_rsp_t)) return false;
      reqserver_rsp_t rsp;
      rsp.trans_id = this->m_req.trans_id;
      rsp.type = REQSERVER_ERROR_RSP;
      buf->write_copy((void *)&rsp, sizeof(reqserver_rsp_t));
      return true;
    } else {
      switch (this->m_req.type) {
        case REQSERVER_READMEM_REQ: {
          if (buf->available() < sizeof(reqserver_rsp_payload_t)) return false;
          reqserver_rsp_payload_t rsp;
          rsp.rsp.trans_id = this->m_req.trans_id;
          rsp.rsp.type = REQSERVER_READMEM_RSP;
          rsp.len = this->m_buf.size();
          buf->write_copy((void *)&rsp, sizeof(reqserver_rsp_payload_t));
          this->m_in_progress = true;
          continue; // loop to start sending data
        }
        case REQSERVER_WRITEMEM_RSP: {
          if (buf->available() < sizeof(reqserver_rsp_t)) return false;
          reqserver_rsp_t rsp;
          rsp.trans_id = this->m_req.trans_id;
          rsp.type = REQSERVER_WRITEMEM_RSP;
          buf->write_copy((void *)&rsp, sizeof(reqserver_rsp_t));
          return true;
        }
      }
      return true;
    }
  }
}

bool ReqServer::Request::execute(std::shared_ptr<Cable> cable) {
  int size = std::min(REQSERVER_MAX_REQ, this->m_req.len - m_pos);
  bool err = cable->access(
    this->m_req.type == REQSERVER_WRITEMEM_REQ,
    this->m_req.addr + this->m_pos,
    size,
    &this->m_buf[this->m_pos]
  );
  if (err) {
    this->m_error = true;
    return true;
  }
  this->m_pos += size;
  return this->m_pos >= this->m_req.len;
}