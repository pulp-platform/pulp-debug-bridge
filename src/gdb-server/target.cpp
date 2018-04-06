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

#include "gdb-server/gdb-server.hpp"
#include <unistd.h>


class Target_cluster_power
{
public:
  virtual bool is_on() { return true; }
};



class Target_cluster_power_bypass : public Target_cluster_power
{
public:
  Target_cluster_power_bypass(Gdb_server *top, uint32_t reg_addr, int bit);
  bool is_on();

private:
  Gdb_server *top;
  uint32_t reg_addr;
  int bit;
};




class Target_cluster_ctrl
{
public:
  virtual bool init() {}
};



class Target_cluster_ctrl_xtrigger : public Target_cluster_ctrl
{
public:
  Target_cluster_ctrl_xtrigger(Gdb_server *top, uint32_t cluster_ctrl_addr);
  bool init();

private:
  Gdb_server *top;
  uint32_t cluster_ctrl_addr;  
};


Target_cluster_ctrl_xtrigger::Target_cluster_ctrl_xtrigger(Gdb_server *top, uint32_t cluster_ctrl_addr)
: top(top), cluster_ctrl_addr(cluster_ctrl_addr)
{

}



bool Target_cluster_ctrl_xtrigger::init()
{
  uint32_t info;
  // set all-stop mode, so that all cores go to debug when one enters debug mode
  info = 0xFFFFFFFF;
  top->cable->access(true, cluster_ctrl_addr + 0x000038, 4, (char*)&info);
}



Target_cluster_power_bypass::Target_cluster_power_bypass(Gdb_server *top, uint32_t reg_addr, int bit)
: top(top), reg_addr(reg_addr), bit(bit)
{
}

bool Target_cluster_power_bypass::is_on()
{
  uint32_t info;
  top->cable->access(false, reg_addr, 4, (char*)&info);
  return (info >> bit) & 1;
}


class Target_cluster
{
public:
  Target_cluster(Gdb_server *top, uint32_t cluster_base, int cluster_id);
  int get_nb_core() { return nb_core; }
  Target_core *get_core(int i) { return cores[i]; }
  void update_power();
  void set_power(bool is_on);
  void resume();
  void halt();

private:
  Gdb_server *top;
  std::vector<Target_core *> cores;
  bool is_on = false;
  Target_cluster_power *power;
  Target_cluster_ctrl *ctrl;
  int nb_on_cores = 0;
  int nb_core = 0;
  int cluster_id;
  uint32_t dbg_unit_addr;
};



Target_core::Target_core(Gdb_server *top, uint32_t dbg_unit_addr)
: top(top), dbg_unit_addr(dbg_unit_addr)
{
  top->log->print(LOG_DEBUG, "Instantiated core\n");
}



void Target_core::read_ppc(uint32_t *ppc)
{
  if (!ppc_is_cached) {
    read(DBG_PPC_REG, &ppc_cached);
    ppc_is_cached = true;
  }
  *ppc = ppc_cached;
}



bool Target_core::gpr_read_all(uint32_t *data)
{
  if (!is_on) return false;
  return top->cable->access(false, dbg_unit_addr + 0x0400, 32 * 4, (char*)data);
}



bool Target_core::gpr_read(unsigned int i, uint32_t *data)
{
  if (!is_on) return false;
  return this->read(0x0400 + i * 4, data);
}



bool Target_core::gpr_write(unsigned int i, uint32_t data)
{
  if (!is_on) return false;
  return this->write(0x0400 + i * 4, data);
}



void Target_core::commit_resume()
{
  this->stopped = false;

  if (!this->is_on) return;

  this->ppc_is_cached = false;
  this->commit_step_mode();
  this->write(DBG_HIT_REG, 0);
}



void Target_core::set_power(bool is_on)
{
  if (is_on != this->is_on)
  {
    this->is_on = is_on;
    if (is_on) {
      top->log->print(LOG_DEBUG, "Setting-on core\n");
      is_on = true;
      // let's discover core id and cluster id
      //this->stop();
      csr_read(0xF14, &hartid);

      cluster_id = hartid >> 5;
      core_id = hartid & 0x1f;

      top->log->print(LOG_DEBUG, "Found a core with id %X (cluster: %d, core: %d)\n", hartid, cluster_id, core_id);
      //write(DBG_IE_REG, 0xFFFF);
      //if (!stopped) resume();
    } else {
      top->log->print(LOG_DEBUG, "Setting-off core (cluster: %d, core: %d)\n", cluster_id, core_id);
      is_on = false;
    }
  }
}

bool Target_core::read(uint32_t addr, uint32_t* rdata)
{
  if (!is_on) return false;
  return top->cable->access(false, dbg_unit_addr + addr, 4, (char*)rdata);
}



bool Target_core::write(uint32_t addr, uint32_t wdata)
{
  if (!is_on) return false;
  return top->cable->access(true, dbg_unit_addr + addr, 4, (char*)&wdata);
}



bool Target_core::csr_read(unsigned int i, uint32_t *data)
{
  if (!is_on) return false;
  return this->read(0x4000 + i * 4, data);
}



bool Target_core::stop()
{
  if (!is_on) return false;

  this->top->log->debug("Halting core (cluster: %d, core: %d)\n", cluster_id, core_id);
  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  data |= 0x1 << 16;
  return this->write(DBG_CTRL_REG, data);
}



bool Target_core::halt()
{
  stopped = true;
  return stop();
}



void Target_core::set_step_mode(bool new_step)
{
  this->top->log->debug("Setting step mode (cluster: %d, core: %d, step: %d, new_step: %d)\n",  cluster_id, core_id, step, new_step);

  if (new_step != step) {
    this->step = new_step;
    this->commit_step = true;
  }
}


void Target_core::commit_step_mode()
{
  if (!is_on) return;

  if (commit_step) {
    this->top->log->debug("Committing step mode (cluster: %d, core: %d, step: %d)\n",  cluster_id, core_id, step);
    this->write(DBG_CTRL_REG, step);
    this->commit_step = false;
  }
}



void Target_core::prepare_resume(bool step)
{

  top->log->debug("Preparing core to resume (step: %d)\n", step);

  // now let's handle software breakpoints
  uint32_t ppc;
  this->read_ppc(&ppc);

  // if there is a breakpoint at this address, let's remove it and single-step over it
  bool has_stepped = false;

#if 0

  if (m_bp->at_addr(ppc)) {

    log->debug("Core is stopped on a breakpoint, stepping to go over (addr: 0x%x)\n", ppc);

    m_bp->disable(ppc);
    dbgif->write(DBG_NPC_REG, ppc); // re-execute this instruction
    dbgif->write(DBG_CTRL_REG, 0x1); // single-step
    while (1) {
      uint32_t value;
      dbgif->read(DBG_CTRL_REG, &value);
      if ((value >> 16) & 1) break;
    }
    m_bp->enable(ppc);
    hasStepped = true;
  }
#endif

  this->set_step_mode(step && !has_stepped);

}



void Target_core::resume()
{
  this->stopped = false;

  if (!is_on) return;

  this->top->log->debug("Resuming core and committing step mode (cluster: %d, core: %d, step: %d)\n",  cluster_id, core_id, step);

  // clear hit register, has to be done before CTRL
  this->write(DBG_HIT_REG, 0);

  this->write(DBG_CTRL_REG, step);

  this->commit_step = false;
  this->ppc_is_cached = false;
}



Target_cluster::Target_cluster(Gdb_server *top, uint32_t cluster_base, int cluster_id)
: top(top), cluster_id(cluster_id), dbg_unit_addr(cluster_base)
{
  js::config *config = top->config;
  js::config *cluster_config = config->get("**/soc/cluster");
  int nb_pe = cluster_config->get("nb_pe")->get_int();
  for (int i=0; i<nb_pe; i++)
  {
    Target_core *core = new Target_core(top, cluster_base + 0x300000 + i * 0x8000);
    cores.push_back(core);
    nb_core++;
  }

  // Figure out if the cluster can be powered down
  js::config *bypass_config = config->get("**/apb_soc_ctrl/regmap/power/bypass");
  if (bypass_config)
  {
    uint32_t addr = config->get("**/apb_soc_ctrl/base")->get_int() +
      bypass_config->get("offset")->get_int();
    int bit = bypass_config->get("content/dbg1/bit")->get_int();

    // Case where there is an apb soc ctrl register which tells if the cluster is on
    power = new Target_cluster_power_bypass(top, addr, bit);
  }
  else
  {
    // Otherwise, the cluster will always be on
    power = new Target_cluster_power();
  }

  ctrl = new Target_cluster_ctrl_xtrigger(top, cluster_base + 0x00200000);

  this->update_power();
}



void Target_cluster::resume()
{
  this->top->log->debug("Resuming cluster (cluster: %d)\n", cluster_id);

  if (dbg_unit_addr != -1) {

    // This cluster is a one with the cross-trigger matrix, use it to resume all cores
    // As the step mode is cached and committed when writing to the individual core
    // debug register, we have to commit it now before resuming the core through the 
    // global register
    for (auto &core: cores) {
      core->commit_resume();
    }

    if (is_on) {
      this->top->log->debug("Resuming cluster through global register (cluster: %d)\n", cluster_id);
      uint32_t info = 0xFFFFFFFF;

      printf("WRITING REG %x\n", dbg_unit_addr + 0x28);

      this->top->cable->access(true, dbg_unit_addr + 0x28, 4, (char*)&info);
    }
  } else {
    // Otherwise, just resume them individually
    for (auto &core: cores) {
      core->resume();
    }
  }
}



void Target_cluster::update_power()
{
  set_power(power->is_on());
}

void Target_cluster::set_power(bool is_on)
{
  if (is_on != this->is_on) {
    this->is_on = is_on;

    ctrl->init();
  }

  if (is_on && nb_on_cores != nb_core)
  {
    for(auto const& core: cores)
    {
      core->set_power(is_on);
    }
  } else {
    nb_on_cores = 0;
  }
}



void Target_cluster::halt()
{
  this->top->log->debug("Halting cluster (cluster: %d)\n", cluster_id);
  // Either the core is alone (FC) or the cluster is using a cross-trigger matrix to stop all cores
  // thus only stop the first one
  cores.front()->halt();
}


#if 0
void Target_cluster::set_power(bool is_on)
{
  if (is_on != this->is_on) {
    this->is_on = is_on;

    if (is_on && base_addr != -1) {
      uint32_t info;
      // set all-stop mode, so that all cores go to debug when one enters debug mode
      info = 0xFFFFFFFF;
      m_mem->access(1, base_addr + 0x000038, 4, (char*)&info);
    }
  }

  if (is_on && nb_on_cores != itfs.size()) {
    uint32_t info = -1;
    if (base_addr != -1) {
      m_mem->access(0, base_addr + 0x000008, 4, (char*)&info);
    }
    int i = 0;
    for (std::list<DbgIF_core*>::iterator it = itfs.begin(); it != itfs.end(); it++, i++) {
      if ((info >> i) & 1) {
        (*it)->set_power(is_on);
      }
    }
  } else {
    nb_on_cores = 0;
  }
}
#endif



Target::Target(Gdb_server *top)
: top(top)
{
  js::config *config = top->config;
  js::config *cluster_config = config->get("**/soc/cluster");
  if (cluster_config != NULL)
  {
    int nb_clusters = config->get("**/nb_cluster")->get_int();
    for (int i=0; i<nb_clusters; i++)
    {
      Target_cluster *cluster = new Target_cluster(top, 0x10000000 + 0x400000 * i, i);

      clusters.push_back(cluster);
      for (int j=0; j<cluster->get_nb_core(); j++)
      {
        Target_core *core = cluster->get_core(j);
        cores.push_back(core);
        cores_from_threadid[core->get_thread_id()] = core;
      }
    }
  }
}




void Target::resume_all()
{
  for (auto &cluster : this->clusters)
  {
    cluster->resume();
  }
}



void Target::resume(bool step)
{
  for (auto &thread : top->target->get_threads())
  {
    thread->prepare_resume(step);
  }

  this->resume_all();
}

bool Target::wait(int socket_client)
{
  printf("UNIMPLEMENTED AT %s %d\n", __FILE__, __LINE__);
  exit(1);
  return true;
}



void Target::update_power()
{
  for (auto &cluster: clusters) {
    cluster->update_power();
  }
}



void Target::halt()
{
  for (auto &cluster: this->clusters)
  {
    cluster->halt();
  }
}