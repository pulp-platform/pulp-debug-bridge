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
 *          Martin Croome, GreenWaves Technologies (martin.croome@greenwaves-technologies.com)
 */

#include "target.hpp"


Target_cluster_cache::Target_cluster_cache(Gdb_server *top, uint32_t addr)
: top(top), addr(addr)
{

}

void Target_cluster_cache::flush()
{
  this->top->log->detail("Flushing cluster cache (addr: 0x%x)\n", addr);

  uint32_t data = 0xFFFFFFFF;
  top->cable->access(true, addr + 0x04, 4, (char*)&data);
}



Target_fc_cache::Target_fc_cache(Gdb_server *top, uint32_t addr)
: top(top), addr(addr)
{

}

void Target_fc_cache::flush()
{
  this->top->log->detail("Flushing FC cache (addr: 0x%x)\n", addr);

  uint32_t data = 0xFFFFFFFF;
  top->cable->access(true, addr + 0x04, 4, (char*)&data);
}


Target_cluster_ctrl_xtrigger::Target_cluster_ctrl_xtrigger(Gdb_server *top, uint32_t cluster_ctrl_addr)
: top(top), cluster_ctrl_addr(cluster_ctrl_addr)
{

}

bool Target_cluster_ctrl_xtrigger::init()
{
  current_mask = 0;
  return this->set_halt_mask(0xFFFFFFFF);
}

bool Target_cluster_ctrl_xtrigger::set_halt_mask(uint32_t mask)
{
  if (current_mask != mask) {
    bool res = top->cable->access(true, cluster_ctrl_addr + 0x000038, 4, (char*)&mask);
    if (res) {
      current_mask = mask;
    }
    return res;
  }
  return false;
}

bool Target_cluster_ctrl_xtrigger::get_halt_mask(uint32_t *mask) { 
  *mask = this->current_mask;
  return true;
}

bool Target_cluster_ctrl_xtrigger::get_halt_status(uint32_t *status)
{
  *status = 0;
  return top->cable->access(true, cluster_ctrl_addr + 0x000028, 4, (char*)status);
}

Target_cluster_power_bypass::Target_cluster_power_bypass(Gdb_server *top, uint32_t reg_addr, int bit)
: top(top), reg_addr(reg_addr), bit(bit)
{
}



bool Target_cluster_power_bypass::is_on()
{
  uint32_t info = 0;
  top->cable->access(false, reg_addr, 4, (char*)&info);
  top->log->debug("Cluster power bypass 0x%08x\n", info);
  return (info >> bit) & 1;
}



Target_core::Target_core(Gdb_server *top, uint32_t dbg_unit_addr, Target_cluster_common * cluster, int core_id)
: top(top), dbg_unit_addr(dbg_unit_addr), cluster(cluster), core_id(core_id)
{
  top->log->print(LOG_DEBUG, "Instantiated core %d:%d\n", this->get_cluster_id(), this->core_id);
  this->thread_id = first_free_thread_id++;
}



void Target_core::init()
{
  top->log->print(LOG_DEBUG, "Init core\n");
  this->is_on = false;
  pc_is_cached = false;
  stopped = false;
  step = false;
  commit_step = false;
}



int Target_core::first_free_thread_id = 0;



void Target_core::flush()
{
  this->top->log->debug("Flushing core prefetch buffer (cluster: %d, core: %d)\n", this->get_cluster_id(), core_id);

  // Write back the value of NPC so that it triggers a flush of the prefetch buffer
  uint32_t npc;
  this->read(DBG_NPC_REG, &npc);
  this->write(DBG_NPC_REG, npc);
}



bool Target_core::gpr_read_all(uint32_t *data)
{
  if (!is_on) return false;
  this->top->log->debug("Reading all registers (cluster: %d, core: %d)\n", this->get_cluster_id(), core_id);

  // Write back the valu
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


bool Target_core::ie_write(uint32_t data)
{
  if (!is_on) return false;
  top->log->print(LOG_DEBUG, "%d:%d -----> TRAP ENABLED\n", this->get_cluster_id(), core_id);
  return this->write(DBG_IE_REG, data);
}

bool Target_core::has_power_state_change()
{
  bool state_changed = this->power_state_changed;
  this->power_state_changed = false;
  return state_changed;
}

bool Target_core::get_power()
{
  return this->is_on;
}

void Target_core::set_power(bool is_on)
{
  top->log->detail("Core %d:%d check power %d -> %d\n", this->get_cluster_id(), core_id, this->is_on, is_on);
  if (is_on != this->is_on)
  {
    top->log->debug("Core %d:%d power state changed\n", this->get_cluster_id(), core_id, this->is_on, is_on);
    this->power_state_changed = true;
    this->pc_is_cached = false; // power state has changed - pc cannot be cached
    this->is_on = is_on;
    if (is_on) {
      top->log->print(LOG_DEBUG, "Core %d:%d on\n", this->get_cluster_id(), core_id);
      // // // let's discover core id and cluster id
      // this->stop();
      // uint32_t hartid_tmp;
      // csr_read(0x014, &hartid_tmp);
      // // csr_read(0xF14, &hartid);
      // top->log->print(LOG_DEBUG, "Read hart id %d\n", hartid_tmp);

      // // cluster_id = hartid >> 5;
      // // core_id = hartid & 0x1f;

      // top->log->print(LOG_DEBUG, "Found a core with id %X (cluster: %d, core: %d)\n", hartid, this->get_cluster_id(), core_id);
      is_on = this->ie_write(1<<3|1<<2); // traps on illegal instructions and ebrks
      // if (!stopped) resume();
    } else {
      top->log->print(LOG_DEBUG, "Core %d:%d off\n", this->get_cluster_id(), core_id);
      is_on = false;
    }
  }
}

bool Target_core::read(uint32_t addr, uint32_t* rdata)
{
  if (!is_on) return false;
  uint32_t offset = dbg_unit_addr + addr;
  bool res = top->cable->access(false, offset, 4, (char*)rdata);
  if (res) {
    top->log->detail("Reading register (addr: 0x%x, contents: 0x%08x)\n", offset, *rdata);
  } else {
    top->log->error("Error reading register (addr: 0x%x)\n", offset);
  }
  return res;
}



bool Target_core::write(uint32_t addr, uint32_t wdata)
{
  if (!is_on) return false;
  uint32_t offset = dbg_unit_addr + addr;
  bool res = top->cable->access(true, offset, 4, (char*)&wdata);
  if (res) {
    top->log->detail("Writing register (addr: 0x%x, value: 0x%x)\n", offset, wdata);
  } else {
    top->log->error("Error writing register (addr: 0x%x)\n", offset);
  }
  return res;
}



bool Target_core::csr_read(unsigned int i, uint32_t *data)
{
  if (!is_on) return false;
  top->log->detail("Reading CSR at offset 0x%08x\n", i);
  return this->read(0x4000 + i * 4, data);
}

bool Target_core::csr_write(unsigned int i, uint32_t data)
{
  if (!is_on) return false;
  top->log->detail("Writing CSR at offset 0x%08x\n", i);
  return this->write(0x4000 + i * 4, data);
}


bool Target_core::is_stopped() {
  if (!is_on) return false;

  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  this->stopped = data & 0x10000;

  top->log->debug("Checking core status (cluster: %d, core: %d, stopped: %d)\n", this->get_cluster_id(), core_id, this->stopped);

  return this->stopped;
}


bool Target_core::stop()
{
  if (!is_on||this->stopped) return false;

  this->top->log->debug("Halting core (cluster: %d, core: %d, is_on: %d)\n", this->get_cluster_id(), core_id, is_on);
  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  data |= 0x1 << 16;
  if (!this->write(DBG_CTRL_REG, data)) {
    fprintf(stderr, "debug_is_stopped: Writing to CTRL reg failed\n");
    return false;
  }
  return true;
}



bool Target_core::halt()
{
  return stop();
}



void Target_core::set_step_mode(bool new_step)
{
  if (new_step != step) {
    this->top->log->debug("Setting step mode (cluster: %d, core: %d, step: %d, new_step: %d)\n",  this->get_cluster_id(), core_id, step, new_step);
    this->step = new_step;
    this->commit_step = true;
  }
}


void Target_core::commit_step_mode()
{
  if (!is_on) return;

  if (commit_step) {
    this->top->log->debug("Committing step mode (cluster: %d, core: %d, step: %d)\n",  this->get_cluster_id(), core_id, step);
    this->write(DBG_CTRL_REG, (1<<16) | step);
    this->commit_step = false;
  }
}

// internal helper functions
bool Target_core::actual_pc_read(unsigned int* pc)
{
  uint32_t npc;
  uint32_t ppc;
  uint32_t cause;
  bool is_hit;
  bool is_sleeping;

  if (pc_is_cached) {
    this->top->log->debug("PC was cached at 0x%08x Core %d:%d (is_BP: %d)\n", 
      pc_cached, this->get_cluster_id(), this->get_core_id(), on_trap);
    *pc = pc_cached;
    return true;
  }

  if (!is_on) return false;

  this->read(DBG_PPC_REG, &ppc);
  this->read(DBG_NPC_REG, &npc);
  this->read_hit(&is_hit, &is_sleeping);

  if (is_hit) {
    *pc = npc;
    on_trap = false;
  } else {
    cause = this->get_cause();
    *pc = (EXC_CAUSE_INTERUPT(cause)||cause==EXC_CAUSE_DBG_HALT ? npc : ppc);
    on_trap = cause == EXC_CAUSE_BREAKPOINT;
  }
  this->top->log->debug("PPC 0x%x NPC 0x%x PC 0x%x Core %d:%d (is_BP: %d)\n", 
    ppc, npc, *pc, this->get_cluster_id(), this->get_core_id(), on_trap);
  pc_cached = *pc;
  pc_is_cached = true;
  return true;
}

bool Target_core::read_hit(bool *is_hit, bool *is_sleeping)
{
  if (!is_on) return false;

  uint32_t hit;
  if (this->read(DBG_HIT_REG, &hit)) {
    *is_hit = step && ((hit & 0x1) == 0x1);
    *is_sleeping = ((hit & 0x10) == 0x10);

    return true;
  } else {
    *is_hit = false; 
    *is_sleeping = false;
    return false;
  }
}

uint32_t Target_core::get_cause()
{
  if (!is_on) return 0x1f;

  uint32_t cause;
  if (!this->read(DBG_CAUSE_REG, &cause)){
    top->log->debug("unable to read cause register\n");
    return false;
  }

  top->log->debug("core %d:%d stop cause %x\n", this->get_cluster_id(), this->get_core_id(), cause);
  return cause;
}


uint32_t Target_core::check_stopped()
{
  bool stopped = this->is_stopped();
  this->top->log->debug("Check core %d stopped %d resume %d\n", core_id, stopped, this->should_resume());

  if (this->should_resume() && stopped) {
    uint32_t cause;
    bool is_hit, is_sleeping;
    this->read_hit(&is_hit, &is_sleeping);
    if (is_hit) {
      this->top->log->debug("core %d:%d tid %d single stepped\n", this->get_cluster_id(), this->get_core_id(), this->get_thread_id()+1);
      return EXC_CAUSE_BREAKPOINT;
    }
    // Experiment for 1f issue
    // if (is_sleeping) {
    //   this->top->log->debug("core %d:%d tid %d is stopped but may be sleeping\n", this->get_cluster_id(), this->get_core_id(), this->get_thread_id()+1);
    //   return EXC_CAUSE_NONE;
    // }
    cause = this->get_cause();
    if (cause == EXC_CAUSE_BREAKPOINT) {
      this->top->log->debug("core %d:%d tid %d hit breakpoint\n", this->get_cluster_id(), this->get_core_id(), this->get_thread_id()+1);
      return cause;
    } else {
      this->top->log->debug("core %d:%d tid %d is stopped with cause 0x%08x\n", this->get_cluster_id(), this->get_core_id(), this->get_thread_id()+1, cause);
      return cause;
    }
  }
  return EXC_CAUSE_NONE;
}



int Target_core::get_cluster_id() { return cluster->get_id(); }

void Target_core::get_name(char* str, size_t len) {
  snprintf(str, len, "Cluster %02d - Core %01d%s", get_cluster_id(), get_core_id(), (get_power()?"":" (Off)"));
}


void Target_core::prepare_resume(bool step)
{
  if (resume_prepared) return;

  top->log->debug("Preparing core %d:%d to resume (step: %d)\n", this->get_cluster_id(), this->get_core_id(), step);

  resume_prepared = true;
  cluster->prepare_resume(step);

  this->set_step_mode(step);
}



void Target_core::commit_resume()
{
  this->stopped = false;
  this->pc_is_cached = false;

  if (!this->is_on) return;

  if (top->bkp->have_changed()) {
    this->flush();
  }

  this->top->log->debug("Commit resume (cluster: %d, core: %d, step: %d)\n",  this->get_cluster_id(), core_id, step);

  this->commit_step_mode();
  // clear hit register, has to be done before CTRL
  if (!this->write(DBG_HIT_REG, 0)) {
    top->log->error("Core %d:%d - unable to clear hit register\n", this->get_cluster_id(), this->get_core_id());
  }
}



void Target_core::resume()
{
  if (!is_on) return;

  this->top->log->debug("Resuming (cluster: %d, core: %d, step: %d)\n",  this->get_cluster_id(), core_id, step);

  if (!this->write(DBG_CTRL_REG, step)) {
    top->log->error("Core %d:%d - unable to write ctrl register\n", this->get_cluster_id(), this->get_core_id());
  }

  uint32_t test;
  this->read(DBG_CTRL_REG, &test);

  if (test != step) {
    top->log->debug("Core %d:%d - wrote 0x%08x got 0x%08x\n", this->get_cluster_id(), this->get_core_id(), step, test);
  } else {
    top->log->debug("Core %d:%d - started ok\n", this->get_cluster_id(), this->get_core_id(), step, test);
  }
}



Target_cluster_common::Target_cluster_common(js::config *, Gdb_server *top, uint32_t cluster_addr, uint32_t xtrigger_addr, int cluster_id)
: top(top), cluster_id(cluster_id), cluster_addr(cluster_addr), xtrigger_addr(xtrigger_addr)
{
    this->top->log->debug("Instantiating cluster %d\n",  cluster_id);
}



Target_cluster_common::~Target_cluster_common()
{
  for (auto &core: cores)
  {
    delete(core);
  }
}



void Target_cluster_common::init()
{
  top->log->print(LOG_DEBUG, "Init cluster %d\n", cluster_id);
  is_on = false;
  nb_on_cores = 0;
  for (auto &core: cores)
  {
    core->init();
  }
}



Target_core * Target_cluster_common::check_stopped(uint32_t *stopped_cause)
{
  *stopped_cause = EXC_CAUSE_NONE;
  Target_core * stopped_core = nullptr;

  this->update_power();
  if (!is_on) return stopped_core;

  top->log->debug("Check if cluster %d stopped\n", get_id());

  for (auto &core: cores)
  {
    // core didn't resume
    if (!core->should_resume()) continue;

    uint32_t cause = core->check_stopped();
    if (cause == EXC_CAUSE_BREAKPOINT) {
      stopped_core = core;
      *stopped_cause = cause;
      break;
#define ONLY_CHECK_ONE
#ifdef ONLY_CHECK_ONE
    } else if (cause != EXC_CAUSE_NONE) {
      if (!stopped_core) {
        stopped_core = core;
        *stopped_cause = cause;
      }
    } else {
      if (!stopped_core && this->ctrl->has_xtrigger()) {
        return nullptr;
      }
    }
#else
    } else if (!stopped_core && cause != EXC_CAUSE_NONE) {
      stopped_core = core;
      *stopped_cause = cause;
    }
#endif
  }
  return stopped_core;
}

void Target_cluster_common::prepare_resume(bool step)
{
  if (resume_prepared) return;

  top->log->debug("Preparing cluster %d to resume (step: %d)\n", cluster_id, step);

  resume_prepared = true;
}

void Target_cluster_common::clear_resume()
{
  resume_prepared = false;
  for (auto &core: cores) {
    core->clear_resume();
  }
}

void Target_cluster_common::flush()
{
  if (!is_on) return;

  this->top->log->debug("Flushing cluster instruction cache (cluster: %d, is_on: %d)\n", cluster_id, is_on);

  if (this->cache)
    this->cache->flush();
}


void Target_cluster_common::commit_resume()
{
  if (!is_on) {
    this->top->log->debug("Cluster %d is off - not committing resume\n", cluster_id);
    return;
  }

  if (!resume_prepared) {
    this->top->log->debug("Cluster %d is not resuming - not committing resume\n", cluster_id);
    return;
  }

  this->top->log->debug("Committing resume (cluster: %d)\n", cluster_id);

  if (top->bkp->have_changed()) {
    this->flush();
  }

  for (auto &core: cores) {
    if (core->should_resume()) {
      core->commit_resume();
    }
  }
}

void Target_cluster_common::resume()
{
  if (!is_on) {
    this->top->log->debug("Cluster %d is off - not resuming\n", cluster_id);
    return;
  }

  if (!resume_prepared) {
    this->top->log->debug("Cluster %d is not resuming\n", cluster_id);
    return;
  }

  this->top->log->debug("Resuming (cluster: %d)\n", cluster_id);

  if (this->ctrl->has_xtrigger()) {

    // This cluster is a one with the cross-trigger matrix, use it to resume all cores
    // As the step mode is cached and committed when writing to the individual core
    // debug register, we have to commit it now before resuming the core through the 
    // global register

    uint32_t xtrigger_mask = 0;
    for (auto &core: cores) {
      if (core->should_resume()) {
        xtrigger_mask|=1<<core->get_core_id();
      }
    }

    ((Target_cluster_ctrl_xtrigger *)this->ctrl)->set_halt_mask(xtrigger_mask);
    this->top->log->debug("Resuming cluster through global register (cluster: %d, mask: %x)\n", cluster_id, xtrigger_mask);
    this->top->cable->access(true, xtrigger_addr + 0x00200000 + 0x28, 4, (char*)&xtrigger_mask);
  } else {
    // Otherwise, just resume them individually
    for (auto &core: cores) {
      if (core->should_resume()) {
        core->resume();
      }
    }
  }
}


void Target_cluster_common::update_power()
{
  set_power(power->is_on());
}


void Target_cluster_common::set_power(bool is_on)
{
  top->log->detail("Cluster %d check power %d -> %d\n", this->cluster_id, this->is_on, is_on);
  if (is_on != this->is_on) {
    top->log->debug("Cluster %d power state changed\n", this->cluster_id, this->is_on, is_on);
    this->is_on = is_on;
    if (this-is_on) {
      this->top->log->debug("Do controller init\n");
      ctrl->init();
    }
  }

  if (is_on) {
    if (nb_on_cores != nb_core)
    {
      nb_on_cores=0;
      this->top->log->debug("Set all on (is_on: %d, nb_on_cores: %d, nb_core: %d)\n", is_on, nb_on_cores, nb_core);
      for(auto const& core: cores)
      {
        core->set_power(is_on);
        nb_on_cores++;
      }
    }
  }
  else
  {
    nb_on_cores = 0;
  }
}



void Target_cluster_common::halt()
{
  // Either the core is alone (FC) or the cluster is using a cross-trigger matrix to stop all cores
  // thus only stop the first one
  this->update_power();
  if (!is_on) {
    this->top->log->debug("Cluster %d is off\n", cluster_id);
    return;
  }
  this->top->log->debug("Halting cluster (cluster: %d)\n", cluster_id);
  cores.front()->halt();
  // Cache all the PCs and halt status and move PCs back over breakpoints before doing anything else
  uint32_t pc;
  for (auto &core: cores)
  {
    core->actual_pc_read(&pc);

    // If there is a breakpoint at the address of the actual program counter
    // it must have executed at the same time as a breakpoint on another
    // core so reexecute it
    if (top->bkp->at_addr(pc) && core->is_stopped_on_trap()) {
      top->log->debug("Core %d:%d was on breakpoint. Re-executing\n", core->get_cluster_id(), core->get_core_id());
      core->write(DBG_NPC_REG, pc); // re-execute this instruction
    }
  }
}



Target_cluster::Target_cluster(js::config *system_config, js::config *config, Gdb_server *top, uint32_t cluster_base, uint32_t xtrigger_addr, int cluster_id)
: Target_cluster_common(config, top, cluster_base, xtrigger_addr, cluster_id)
{
  int nb_pe = config->get("nb_pe")->get_int();
  top->log->debug("creating cluster %d with %d cores\n", cluster_id, nb_pe);
  for (int i=0; i<nb_pe; i++)
  {
    Target_core *core = new Target_core(top, cluster_base + 0x300000 + i * 0x8000, this, i);
    cores.push_back(core);
    nb_core++;
  }

  // Figure out if the cluster can be powered down
  js::config *bypass_config = system_config->get("**/apb_soc_ctrl/regmap/power/bypass");
  top->log->debug("cluster %d power bypass %d\n", cluster_id, bypass_config != NULL);
  if (bypass_config)
  {
    uint32_t addr = system_config->get("**/apb_soc_ctrl/base")->get_int() +
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

  cache = new Target_cluster_cache(top, cluster_base + 0x00201400);

  this->update_power();
}



Target_fc::Target_fc(js::config *config, Gdb_server *top, uint32_t fc_dbg_base, uint32_t fc_cache_base, int cluster_id)
: Target_cluster_common(config, top, fc_dbg_base, -1, cluster_id)
{
  Target_core *core = new Target_core(top, fc_dbg_base, this, 0);
  cores.push_back(core);
  nb_core++;

  // the FC will always be on
  power = new Target_cluster_power();

  ctrl = new Target_cluster_ctrl();

  if (fc_cache_base != 0xffffffff)
    cache = new Target_fc_cache(top, fc_cache_base);

  this->update_power();
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
  top->log->debug("Init target\n");
  js::config *config = top->config;

  js::config *fc_config = config->get("**/soc/fc");
  if (fc_config != NULL)
  {
    unsigned int fc_dbg_addr = config->get("**/fc_dbg_unit/base")->get_int();
    js::config *cache_config = config->get("**/fc_icache/base");
    unsigned int fc_icache_addr = -1;
    if (cache_config)
      fc_icache_addr = cache_config->get_int();

    Target_fc *cluster = new Target_fc(fc_config, top, fc_dbg_addr, fc_icache_addr, fc_config->get("cluster_id")->get_int());

    clusters.push_back(cluster);
    Target_core *core = cluster->get_core(0);
    top->log->debug("Init FC Core %d:%d Thread Id %d\n", core->get_cluster_id(), core->get_core_id(), core->get_thread_id());
    cores.push_back(core);
    cores_from_threadid[core->get_thread_id()] = core;
  }

  js::config *cluster_config = config->get("**/soc/cluster");
  if (cluster_config != NULL)
  {
    int nb_clusters = config->get("**/nb_cluster")->get_int();
    for (int i=0; i<nb_clusters; i++)
    {
      unsigned int cluster_base = 0x10000000;
      js::config *base_config = config->get("**/cluster/base");
      if (base_config != NULL)
        cluster_base = base_config->get_int();

      Target_cluster *cluster = new Target_cluster(config, cluster_config, top, cluster_base + 0x400000 * i, cluster_base + 0x400000 * i, i);

      clusters.push_back(cluster);
      for (int j=0; j<cluster->get_nb_core(); j++)
      {
        Target_core *core = cluster->get_core(j);
        top->log->debug("Init Cluster Core %d:%d Thread Id %d\n", core->get_cluster_id(), core->get_core_id(), core->get_thread_id());
        cores.push_back(core);
        cores_from_threadid[core->get_thread_id()] = core;
      }
    }
  }
  top->log->debug("Finish target init\n");
}

Target::~Target()
{
  for (auto &cluster : this->clusters)
  {
    delete(cluster);
  }
}

void Target::flush()
{
  for (auto &cluster : this->clusters)
  {
    cluster->flush();
  }
}


void Target::clear_resume_all()
{
  for (auto &cluster : this->clusters)
  {
    cluster->clear_resume();
  }
}

void Target::prepare_resume_all(bool step)
{
  for (auto &thread : this->get_threads())
  {
    thread->prepare_resume(step);
  }
}

void Target::resume_all()
{
  started = true;
  for (auto &cluster : this->clusters)
  {
    cluster->commit_resume();
  }
  for (auto &cluster : this->clusters)
  {
    cluster->resume();
  }
  // all the cores have resumed so clear the enable / disable history
  top->bkp->clear_history();
}

Target_core * Target::check_stopped()
{
  this->top->log->debug("Check if target stopped\n");
  Target_core * stopped_core = NULL;
  for (auto &cluster : this->clusters)
  {
    uint32_t cause;

    Target_core * core = cluster->check_stopped(&cause);
    if (cause == EXC_CAUSE_BREAKPOINT) {
      stopped_core = core;
      break;
    } else if (!stopped_core && cause != EXC_CAUSE_NONE) {
      stopped_core = core;
    }
  }
  return stopped_core;
}

void Target::reinitialize()
{
  this->top->log->debug("Reinitialize target\n");
  for (auto &cluster: clusters) {
    cluster->init();
  }
}

void Target::update_power()
{
  for (auto &cluster: clusters) {
    cluster->update_power();
  }
}

bool Target::mem_read(uint32_t addr, uint32_t length, char * buffer)
{
  bool ret = top->cable->access(false, addr, length, buffer);
  top->log->detail("read memory (addr: 0x%08x, len: %d, ret: %d)\n", addr, length, ret);
  return ret;
}

bool Target::mem_write(uint32_t addr, uint32_t length, char * buffer)
{
  bool ret = top->cable->access(true, addr, length, buffer);
  top->log->detail("write memory (addr: 0x%08x, len: %d, ret: %d)\n", addr, length, ret);
  return ret;
}

void Target::halt()
{
  if (!started) return;
  started = false;
  for (auto &cluster: this->clusters)
  {
    cluster->halt();
  }
}