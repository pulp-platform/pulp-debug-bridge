/*
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
 * Authors: Andreas Traber
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "adv_dbg_itf.hpp"
#ifdef __USE_FTDI__
#include "cables/ftdi/ftdi.hpp"
#endif

#define JTAG_SOC_AXIREG  4


Adv_dbg_itf::Adv_dbg_itf(js::config *system_config, Log* log, Cable *m_dev) : config(system_config), log(log), m_dev(m_dev)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&mutex, &attr);

  js::config *conf = system_config->get("**/adv_dbg_unit/debug_ir");

  this->debug_ir = conf != NULL ? conf->get_int() : 0x4;
  log->debug("Using debug IR: 0x%x\n", this->debug_ir);


  conf = system_config->get("**/adv_dbg_unit/retry_count");

  this->retry_count = conf != NULL ? conf->get_int() : 0;
  log->debug("Using retry count: %d\n", this->retry_count);


  conf = NULL; //system_config->get("**/adv_dbg_unit/check_errors");

  this->check_errors = conf != NULL ? conf->get_bool() : false;
  log->debug("Checking errors: %d\n", this->check_errors);

}



Adv_dbg_itf::~Adv_dbg_itf()
{
  delete m_dev;
}



bool Adv_dbg_itf::connect(js::config *config)
{
  js::config *jtag_cable_config = NULL;

  access_timeout = config->get_int("**/access_timeout_us");
  if (access_timeout == 0)
    access_timeout = 1000000;

  log->debug ("Using access timeout: %d us\n", access_timeout);

  if (!m_dev->connect(config)) {
    log->error("Could not connect to JTAG device\n");
    return false;
  }

  m_dev->jtag_reset(true);
  m_dev->jtag_reset(false);

  this->jtag_soft_reset();

  // now we can work with the chain
  if (!jtag_auto_discovery()) {
    log->error("Did not find an adv debug unit in the chain, exiting\n");
    return false;
  }

  return true;
}



bool Adv_dbg_itf::jtag_reset(bool active)
{
  pthread_mutex_lock(&mutex);

  for (int i=0; i < m_jtag_devices.size(); i++)
  {
    m_jtag_devices[i].is_in_debug = false;
  }
  bool result = m_dev->jtag_reset(active);

  pthread_mutex_unlock(&mutex);

  return result;
}



bool Adv_dbg_itf::chip_reset(bool active)
{
  bool result = true;

  pthread_mutex_lock(&mutex);

  if (!m_dev->chip_reset(active)) { result = false; goto end; };
  // Wait some time so that we don't do any IO access after that while the chip
  // has not finished booting
  if (!active) usleep(10000);

end:
  pthread_mutex_unlock(&mutex);

  return result;
}


void Adv_dbg_itf::device_select(unsigned int i)
{
  pthread_mutex_lock(&mutex);

  m_jtag_device_sel = i;

  if (i == m_jtag_devices.size() - 1)
    m_tms_on_last = 1;
  else
    m_tms_on_last = 0;

  pthread_mutex_unlock(&mutex);
}



bool Adv_dbg_itf::access(bool wr, unsigned int addr, int size, char* buffer)
{
  bool result;

  pthread_mutex_lock(&mutex);

  jtag_debug();

  if (wr)
    result = write(addr, size, buffer);
  else
    result = read(addr, size, buffer);

  pthread_mutex_unlock(&mutex);

  return result;
}



bool Adv_dbg_itf::write(unsigned int _addr, int _size, char* _buffer)
{
  int count = 0;
  do
  {
    unsigned int addr = _addr;
    int size = _size;
    char *buffer = _buffer;

    bool retval = true;

    if (addr & 0x1 && size >= 1) {
      retval = retval && write_internal(AXI_WRITE8, addr, 1, buffer);
      size   -= 1;
      buffer += 1;
      addr   += 1;
    }

    if (addr & 0x2 && size >= 2) {
      retval = retval && write_internal(AXI_WRITE16, addr, 2, buffer);
      size   -= 2;
      buffer += 2;
      addr   += 2;
    }

  #if 0

    // TODO add 64 bits support and make sure it is not used on pulp targets

    if (addr & 0x4 && size >= 4) {
      retval = retval && write_internal(AXI_WRITE32, addr, 4, buffer);
      size   -= 4;
      buffer += 4;
      addr   += 4;
    }

    if (size >= 8) {
      int local_size = size & (~0x7);
      retval = retval && write_internal(AXI_WRITE64, addr, local_size, buffer);
      size   -= local_size;
      buffer += local_size;
      addr   += local_size;
    }

  #else

    if (size >= 4) {
      int local_size = size & (~0x3);

       while (local_size)
       {
         int iter_size = local_size;
         if (iter_size > 1024) iter_size = 1024;

         retval = retval && write_internal(AXI_WRITE32, addr, iter_size, buffer);
         local_size   -= iter_size;
         size   -= iter_size;
         buffer += iter_size;
         addr   += iter_size;
       }
     }
  #endif

    if (size >= 2) {
      retval = retval && write_internal(AXI_WRITE16, addr, 2, buffer);
      size   -= 2;
      buffer += 2;
      addr   += 2;
    }

    if (size >= 1) {
      retval = retval && write_internal(AXI_WRITE8, addr, 1, buffer);
      size   -= 1;
      buffer += 1;
      addr   += 1;
    }

    if (this->check_errors)
    {
      uint32_t error_addr;
      bool error = false;
      retval = retval && read_error_reg(&error_addr, &error);

      if (error) {
        log->debug("advdbg reports: Failed to write to addr %X\n", error_addr);
        count++;
        continue;
      }
    }

    return retval;
  } while (count < this->retry_count);

  return false;
}



bool Adv_dbg_itf::read(unsigned int _addr, int _size, char* _buffer)
{
  int count = 0;
  do
  {
    unsigned int addr = _addr;
    int size = _size;
    char *buffer = _buffer;

    bool retval = true;

    if (addr & 0x1 && size >= 1) {
      retval = retval && read_internal(AXI_READ8, addr, 1, buffer);
      size   -= 1;
      buffer += 1;
      addr   += 1;
    }

    if (addr & 0x2 && size >= 2) {
      retval = retval && read_internal(AXI_READ16, addr, 2, buffer);
      size   -= 2;
      buffer += 2;
      addr   += 2;
    }

  #if 0

    // TODO add 64 bits support and make sure it is not used on pulp targets

    if (addr & 0x4 && size >= 4) {
      retval = retval && read_internal(AXI_READ32, addr, 4, buffer);
      size   -= 4;
      buffer += 4;
      addr   += 4;
    }

    if (size >= 8) {
      int local_size = size & (~0x7);
      retval = retval && read_internal(AXI_READ64, addr, local_size, buffer);
      size   -= local_size;
      buffer += local_size;
      addr   += local_size;
    }
    
  #else

    if (size >= 4) {
      int local_size = size & (~0x3);

      while (local_size)
      {
        int iter_size = local_size;
        if (iter_size > 2048) iter_size = 2048  ;

        retval = retval && read_internal(AXI_READ32, addr, iter_size, buffer);
        local_size   -= iter_size;
        size   -= iter_size;
        buffer += iter_size;
        addr   += iter_size;
      }
    }

  #endif

    if (size >= 2) {
      retval = retval && read_internal(AXI_READ16, addr, 2, buffer);
      size   -= 2;
      buffer += 2;
      addr   += 2;
    }

    if (size >= 1) {
      retval = retval && read_internal(AXI_READ8, addr, 1, buffer);
      size   -= 1;
      buffer += 1;
      addr   += 1;
    }

    if (this->check_errors)
    {
      uint32_t error_addr;
      bool error = false;
      retval = retval && read_error_reg(&error_addr, &error);

      if (error) {
        log->debug("advdbg reports: Failed to read from addr %X\n", error_addr);
        count++;
        continue;
      }
    }

    return retval;
  } while (count < retry_count);

  return false;
}



bool Adv_dbg_itf::write_internal(ADBG_OPCODES opcode, unsigned int addr, int size, char* buffer)
{
  char buf[8];
  char recv[1];
  int bitwidth;
  uint32_t crc;

  switch(opcode) {
    case AXI_WRITE8:
      bitwidth = 8;
      break;

    case AXI_WRITE16:
      bitwidth = 16;
      break;

    case AXI_WRITE32:
      bitwidth = 32;
      break;

    case AXI_WRITE64:
      bitwidth = 64;
      break;

    default:
      log->warning("Invalid opcode: %d\n", opcode);
      return false;
  }

  if (size % (bitwidth/8) != 0) {
    log->warning("Size is not aligned to selected bitwidth\n");
    return false;
  }

  jtag_axi_select();

  jtag_pad_before();

  // build burst setup command
  // bit 52: 0
  // bit 51-48: opcode
  // bit 47:16: address
  // bit 15:0:  count
  buf[6] = opcode;
  buf[5] = addr >> 24;
  buf[4] = addr >> 16;
  buf[3] = addr >>  8;
  buf[2] = addr >>  0;
  buf[1] = ( size / (bitwidth / 8) ) >> 8;
  buf[0] = ( size / (bitwidth / 8) ) >> 0;

  if (!m_dev->stream_inout(NULL, buf, 53, m_tms_on_last)) {
    log->warning("ft2232: failed to write opcode stream to device\n");
    return false;
  }

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR
  m_dev->jtag_write_tms(0); // shift DR

  jtag_pad_before();

  // send start bit
  if (!m_dev->bit_inout(NULL, 0x1, false)) {
    log->warning("ft2232: failed to write start bit to device\n");
    return false;
  }

  // send data
  if (!m_dev->stream_inout(NULL, buffer, size * 8, false)) {
    log->warning("ft2232: failed to write data to device\n");
    return false;
  }

  // send crc
  crc = crc_compute(0xFFFFFFFF, buffer, size * 8);
  buf[3] = crc >> 24;
  buf[2] = crc >> 16;
  buf[1] = crc >>  8;
  buf[0] = crc >>  0;
  if (!m_dev->stream_inout(NULL, buf, 32, false)) {
    log->warning("ft2232: failed to write crc to device\n");
    return false;
  }

  // push crc all the way in before we can expect to receive the match bit
  jtag_pad_after(false);

  // receive match bit
  recv[0] = 0;

  if (!m_dev->stream_inout(recv, buf, 2, false)) {
    log->warning("ft2232: failed to read match bit from device\n");
    return false;
  }


  m_dev->jtag_write_tms(1); // exit 1 DR
  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(0); // run test idle

  if ((recv[0] & 0x1) != 0x1) {
    // TODO some pulp targets like fulmine does not support CRC.
    log->warning("ft2232: Match bit was not set. Transfer has probably failed; addr %08X, size %d\n", addr, size);
    return false;
  }

  return true;
}



bool Adv_dbg_itf::read_internal(ADBG_OPCODES opcode, unsigned int addr, int size, char* buffer)
{
  char recv[size];
  char buf[size < 8 ? 8 : size];
  int bytewidth;
  int nwords;
  uint32_t crc = 0xFFFFFFFF;

  switch(opcode) {
    case AXI_READ8:
      bytewidth = 1;
      break;

    case AXI_READ16:
      bytewidth = 2;
      break;

    case AXI_READ32:
      bytewidth = 4;
      break;

    case AXI_READ64:
      bytewidth = 8;
      break;

    default:
      log->warning("Invalid opcode: %d\n", opcode);
      return false;
  }

  int factor = 1;

  // Increase the word size in case we have big burst with good multiple
  if (size >= 256 && size % 256 == 0)
  {
    bytewidth = 256;
    factor = bytewidth / 4;
  }

  if (size % bytewidth != 0) {
    log->warning("Size is not aligned to selected bitwidth\n");
    return false;
  }

  nwords = size / bytewidth;

  jtag_axi_select();

  jtag_pad_before();

  // build burst setup command
  // bit 52: 0
  // bit 51-48: opcode
  // bit 47:16: address
  // bit 15:0:  count
  buf[6] = opcode;
  buf[5] = addr >> 24;
  buf[4] = addr >> 16;
  buf[3] = addr >>  8;
  buf[2] = addr >>  0;
  buf[1] = (nwords * factor) >> 8;
  buf[0] = (nwords * factor) >> 0;

  if (!m_dev->stream_inout(NULL, buf, 53, m_tms_on_last)) {
    log->warning("ft2232: failed to write opcode stream to device\n");
    return false;
  }

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR
  m_dev->jtag_write_tms(0); // shift DR

  // no need to do padding here, we just wait for a 1

  // wait for a '1' from the AXI module
  struct timeval start, now;
  int retval = gettimeofday(&start, NULL);
  assert(retval == 0);

  while (true) {
    buf[0] = 0x0;
    if (!m_dev->bit_inout(buf, 0x0, false)) {
      log->warning("ft2232: failed to read start bit from device\n");
      return false;
    }

    if (buf[0] & 0x1)
      break;

    retval = gettimeofday(&now, NULL);
    assert(retval == 0);
    unsigned long usec_elapsed = (now.tv_sec - start.tv_sec) * 1000000 + (now.tv_usec - start.tv_usec);

    if (usec_elapsed > access_timeout) {
      log->warning("ft2232: did not get a start bit from the AXI module in 1s\n");
      return false;
    }
  }

  // make sure we only send 0's to the device
  memset(buf, 0, size);

  // receive data
  crc = 0xFFFFFFFF;
  for (int i = 0; i < nwords; i++) {
    if (!m_dev->stream_inout(recv, buf, bytewidth*8, false)) {
      log->warning("ft2232: failed to receive data from device\n");
      return false;
    }

    memcpy(buffer, recv, bytewidth);
    crc = crc_compute(crc, recv, bytewidth*8);

    buffer = buffer + bytewidth;
  }

  // receive crc
  if (!m_dev->stream_inout(recv, buf, 33, m_tms_on_last)) {
    log->warning("ft2232: failed to read crc from device\n");
    return false;
  }

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(0); // run test idle

  uint32_t recv_crc;
  memcpy(&recv_crc, recv, 4);
  if (crc != recv_crc) {
    log->warning ("ft2232: crc from adv dbg unit did not match for request to addr %08X\n", addr);
    log->debug ("ft2232: Got %08X, expected %08X\n", recv_crc, crc);
    return false;
  }

  return true;
}



bool Adv_dbg_itf::read_error_reg(uint32_t *addr, bool *error)
{
  char buf[5];
  assert (addr != NULL);
  assert (error != NULL);

  jtag_axi_select();

  jtag_pad_before();

  // build internal register select
  // 63    = 0    (module_cmd)
  // 62:59 = 1101 (operation_in)
  // 58    = 0    (does not matter)
  // => 6 bits
  buf[0] = 0x1A;

  if (!m_dev->stream_inout(NULL, buf, 6, m_tms_on_last)) {
    log->warning("ft2232: failed to write internal register select to device\n");
    return false;
  }

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR
  m_dev->jtag_write_tms(0); // shift DR

  jtag_pad_before();

  memset(buf, 0, 5);

  if (!m_dev->stream_inout(buf, buf, 33, m_tms_on_last)) {
    log->warning("ft2232: failed to read AXI error register\n");
    return false;
  }

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(0); // run test idle

  *error = buf[0] & 0x1;

  // shift buffer by 1 bit to the right to get the address
  for (int i = 0; i < 4; i++)
    buf[i] = buf[i] >> 1 | ((buf[i+1] & 0x1) << 31);
  memcpy(addr, buf, 4);

  if (*error) {
    // there was an error, so we have to clear the internal error register on
    // the adv dbg unit
    return clear_error_reg();
  }

  return true;
}



bool Adv_dbg_itf::clear_error_reg()
{
  char buf[5];

  // IMPORTANT: since there is a bug in hardware, if the next access is a
  // write, it will fail (or at least look like it failed). The reason for this
  // is that the error register kind of keeps hanging in an error state. To
  // mitigate this problem, we perform one valid dummy access
  //read_internal(AXI_READ8, 0x10000000, 1, buf);

  jtag_axi_select();

  jtag_pad_before();

#if 0
  // build internal register select
  // 63    = 0    (module_cmd)
  // 62:59 = 1001 (operation_in)
  // 58    = 0    (does not matter)
  // => 6 bits
  // 55:46 = 1 (all 1)
  // => +16 bits
  buf[2] = 0x09;
  buf[1] = 0x00;
  buf[0] = 0x08;

  if (!m_dev->stream_inout(NULL, buf, 5+1+15, m_tms_on_last)) {
    log->warning("ft2232: failed to write internal register write to device\n");
    return false;
  }
#else
  buf[0] = (0x9 << 1) | 1;

  if (!m_dev->stream_inout(NULL, buf, 5+1, m_tms_on_last)) {
    log->warning("ft2232: failed to write internal register write to device\n");
    return false;
  }
#endif

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR

  return true;
}



bool Adv_dbg_itf::jtag_set_selected_ir(char ir)
{
  char buf[1];
  bool is_last;
  unsigned int i;

  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(1); // select IR scan
  m_dev->jtag_write_tms(0); // capture IR
  m_dev->jtag_write_tms(0); // shift IR

  // put devices into bypass mode if they are before our device
  for (i = 0; i < m_jtag_devices.size(); i++) {

    // set our device into debug, the rest into bypass
    if(i == m_jtag_device_sel)
      buf[0] = ir;
    else
      buf[0] = 0xFF;

    is_last = (m_jtag_devices.size() - 1 == i);

    if (!m_dev->stream_inout(NULL, buf, m_jtag_devices[i].ir_len, is_last)) {
      log->warning ("ft2232: failed to set IR to bypass\n");
      return false;
    }
  }

  m_dev->jtag_write_tms(1); // update IR
  m_dev->jtag_write_tms(0); // run test idle

  return true;
}



bool Adv_dbg_itf::jtag_debug()
{
  jtag_device &dev = m_jtag_devices[m_jtag_device_sel];
  if (!dev.is_in_debug)
  {
    jtag_soft_reset();
    dev.is_in_debug = jtag_set_selected_ir(this->debug_ir);
    return dev.is_in_debug;
  }
}



bool Adv_dbg_itf::jtag_axi_select()
{
  char buf[1];
  buf[0] = 0x20;

  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR scan
  m_dev->jtag_write_tms(0); // shift DR

  jtag_pad_before();

  if (!m_dev->stream_inout(NULL, buf, 6, m_tms_on_last)) {
    log->warning("ft2232: failed to write AXI select to device\n");
    return false;
  }

  jtag_pad_after(!m_tms_on_last);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR scan
  m_dev->jtag_write_tms(0); // shift DR

  m_dev->flush();

  return true;
}

#define MAX_CHAIN_LEN 128


bool Adv_dbg_itf::jtag_auto_discovery()
{
  char recv_buf[MAX_CHAIN_LEN/8];
  char send_buf[MAX_CHAIN_LEN/8];
  int ir_len;
  int dr_len;

  ir_len = ir_len_detect();
  jtag_soft_reset();
  dr_len = dr_len_detect();

  log->debug("JTAG IR len is %d, DR len is %d\n", ir_len, dr_len);

  std::string chip = this->config->get("**/chip/name")->get_str();

  if (chip != "wolfe")
  {
    if (dr_len <= 0 || ir_len <= 0) {
      log->error("JTAG sanity check failed\n");
      return false;
    }
  }
  else
  {
    // On wolfe, due to a HW bug, it is not possible to guess the dr len
    dr_len = 32;
  }

  // since we now know how long the chain is, we can shift out the IDs
  jtag_soft_reset();
  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR scan
  m_dev->jtag_write_tms(0); // shift DR scan

  m_dev->stream_inout(recv_buf, send_buf, dr_len, true);

  m_jtag_devices.clear();

  // build 32-bit chunks for the IDs
  for(int i = 0; i < dr_len/32; i++) {
    jtag_device device;
    device.id  = (recv_buf[i*4 + 3] & 0xFF) << 24;
    device.id |= (recv_buf[i*4 + 2] & 0xFF) << 16;
    device.id |= (recv_buf[i*4 + 1] & 0xFF) <<  8;
    device.id |= (recv_buf[i*4 + 0] & 0xFF) <<  0;
    device.index  = i;
    device.is_in_debug = false;
    // TODO the detacted IR length is wrong when there are several taps
    device.ir_len = 4;

    log->debug("Device %d ID: %08X\n", i, device.id);

    m_jtag_devices.push_back(device);
  }

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(0); // run test idle

  return true;
}



int Adv_dbg_itf::ir_len_detect()
{
  char recv_buf[MAX_CHAIN_LEN/8];
  char send_buf[MAX_CHAIN_LEN/8];
  int jtag_chainlen = -1;

  jtag_soft_reset();

  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(1); // select IR scan
  m_dev->jtag_write_tms(0); // capture IR
  m_dev->jtag_write_tms(0); // shift IR

  // first poison with 0
  memset(send_buf, 0, MAX_CHAIN_LEN/8);
  m_dev->stream_inout(recv_buf, send_buf, MAX_CHAIN_LEN, false);

  if (recv_buf[MAX_CHAIN_LEN/8-1] != 0)
    log->warning("ft2232: Did not receive 0 that we sent, JTAG chain might be faulty\n");

  // now we send all 1's and see how long it takes for them to get back to us
  memset(send_buf, 0xFF, MAX_CHAIN_LEN/8);

  m_dev->stream_inout(recv_buf, send_buf, MAX_CHAIN_LEN, true);

  for(int i = 0; i < MAX_CHAIN_LEN; i++) {
    if ((recv_buf[i/8] >> (i%8)) & 0x1) {
      jtag_chainlen = i;
      break;
    }
  }
  log->debug("ft2232: jtag_chainlen = %d\n", jtag_chainlen);

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(0); // run test idle

  return jtag_chainlen;
}



int Adv_dbg_itf::dr_len_detect()
{
  char recv_buf[MAX_CHAIN_LEN/8];
  char send_buf[MAX_CHAIN_LEN/8];
  int jtag_chainlen = -1;

  m_dev->jtag_write_tms(1); // select DR scan
  m_dev->jtag_write_tms(0); // capture DR scan
  m_dev->jtag_write_tms(0); // shift DR scan

  // first poison with 0
  memset(send_buf, 0, MAX_CHAIN_LEN/8);

  m_dev->stream_inout(recv_buf, send_buf, MAX_CHAIN_LEN, false);

  if (recv_buf[MAX_CHAIN_LEN/8-1] != 0)
    log->warning("ft2232: Did not receive 0 that we sent, JTAG chain might be faulty\n");

  // now we send all 1's and see how long it takes for them to get back to us
  memset(send_buf, 0xFF, MAX_CHAIN_LEN/8);

  m_dev->stream_inout(recv_buf, send_buf, MAX_CHAIN_LEN, true);

  for(int i = 0; i < MAX_CHAIN_LEN; i++) {
    if ((recv_buf[i/8] >> (i%8)) & 0x1) {
      jtag_chainlen = i;
      break;
    }
  }

  m_dev->jtag_write_tms(1); // update DR
  m_dev->jtag_write_tms(0); // run test idle

  return jtag_chainlen;
}


bool Adv_dbg_itf::jtag_soft_reset()
{
  pthread_mutex_lock(&mutex);

  for (int i=0; i < m_jtag_devices.size(); i++)
  {
    m_jtag_devices[i].is_in_debug = false;
  }
  bool result = m_dev->jtag_soft_reset();

  pthread_mutex_unlock(&mutex);

  return result;
}
  



#define ADBG_CRC_POLY 0xedb88320

uint32_t Adv_dbg_itf::crc_compute(uint32_t crc, char* data_in, int length_bits)
{
  uint32_t d, c;

  for(int i = 0; i < length_bits; i++)
  {
    d = ((data_in[i / 8] >> (i % 8)) & 0x1) ? 0xffffffff : 0;
    c = (crc & 0x1) ? 0xffffffff : 0;
    crc = crc >> 1;
    crc = crc ^ ((d ^ c) & ADBG_CRC_POLY);
  }

  return crc;
}



bool Adv_dbg_itf::jtag_pad_before()
{
  if (m_jtag_device_sel == 0) {
    return true;
  }

  unsigned int pad_bits = m_jtag_device_sel;
  char* buffer = (char*)malloc(pad_bits/8);
  memset(buffer, 0, pad_bits/8);

  if (!m_dev->stream_inout(NULL, buffer, pad_bits, false)) {
    log->warning("ft2232: failed to pad chain before our selected device\n");
    return false;
  }

  free(buffer);

  return true;
}



bool Adv_dbg_itf::jtag_pad_after(bool tms)
{
  if (m_jtag_device_sel == m_jtag_devices.size() - 1) {
    return true;
  }

  unsigned int pad_bits = m_jtag_devices.size() - m_jtag_device_sel - 1;
  char* buffer = (char*)malloc(pad_bits/8);
  memset(buffer, 0, pad_bits/8);

  if (!m_dev->stream_inout(NULL, buffer, pad_bits, tms)) {
    log->warning("ft2232: failed to pad chain before our selected device\n");
    return false;
  }

  free(buffer);

  return true;
}

bool Adv_dbg_itf::bit_inout(char* inbit, char outbit, bool last)
{
  pthread_mutex_lock(&mutex);

  // Invalidate debug mode in case the caller is sending raw bitstream as it might
  // change the IR
  m_jtag_devices[m_jtag_device_sel].is_in_debug = false;
  bool result = m_dev->bit_inout(inbit, outbit, last);

  pthread_mutex_unlock(&mutex);

  return result;
}

bool Adv_dbg_itf::stream_inout(char* instream, char* outstream, unsigned int n_bits, bool last)
{
  pthread_mutex_lock(&mutex);

  // Invalidate debug mode in case the caller is sending raw bitstream as it might
  // change the IR
  m_jtag_devices[m_jtag_device_sel].is_in_debug = false;
  bool result = m_dev->stream_inout(instream, outstream, n_bits, last);

  pthread_mutex_unlock(&mutex);

  return result;
}

int Adv_dbg_itf::flush()
{
  pthread_mutex_lock(&mutex);

  bool result = m_dev->flush();

  pthread_mutex_unlock(&mutex);

  return result;
}

void Adv_dbg_itf::lock()
{
  pthread_mutex_lock(&mutex);
}

void Adv_dbg_itf::unlock()
{
  pthread_mutex_unlock(&mutex);
}
