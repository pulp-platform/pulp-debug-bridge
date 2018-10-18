/*
 * Copyright (C) 2018 ETH Zurich, University of Bologna and GreenWaves Technologies SA
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
 * Authors: Martin Croome, GWT (martin.croome@greenwaves.technologies.com)
 */

#include "loops.hpp"

LoopManager::LoopManager(
        const EventLoop::SpEventLoop &event_loop, std::shared_ptr<Cable> cable, unsigned int debug_struct_addr, 
        int64_t slow_usecs, int64_t fast_usecs, bool check_exit) : 
            log("LOOPM"), m_cable(std::move(cable)), m_debug_struct_addr(debug_struct_addr),
            m_slow_usecs(slow_usecs), m_fast_usecs(fast_usecs), m_check_exit(check_exit) {
    m_loop_te = event_loop->getTimerEvent(std::bind(&LoopManager::run_loops, this));
}

LoopManager::~LoopManager() {
    clear_loopers();
}

void LoopManager::set_debug_struct_addr(unsigned int debug_struct_addr) {
    m_debug_struct_addr = debug_struct_addr;
}

void LoopManager::access(bool write, unsigned int addr, int len, char * buf)
{
    if (!m_cable->access(write, addr, len, buf))
        throw LoopCableException();
}

void LoopManager::flush() {
    try {
#if defined(__NEW_REQLOOP__) && defined(__CHECK_AVAILABILITY__)
        if (!target_is_available()) return;
#endif
        hal_debug_struct_t * debug_struct = activate();
        if (debug_struct == NULL) return;
        if (m_check_exit && program_has_exited(debug_struct)) return;

        auto ilooper = m_loopers.begin();

        while (ilooper != m_loopers.end() && target_is_available()) {
            auto looper = *ilooper;
            if (looper->get_paused()) {
                ilooper++;
                continue;
            }
            looper->flush(debug_struct);
            ilooper++;
        }
    } catch (LoopCableException e) {
        log.error("Loop manager cable error during flush\n");
    }
}

int64_t LoopManager::run_loops() {
    try {
#if defined(__NEW_REQLOOP__) && defined(__CHECK_AVAILABILITY__)
        if (!target_is_available()) return 0;
#endif
        hal_debug_struct_t * debug_struct = activate();
        if (debug_struct == NULL) return (m_cur_usecs==kEventLoopTimerDone?m_cur_usecs:0);
        if (m_check_exit && program_has_exited(debug_struct)) {
            return kEventLoopTimerDone;
        }
        auto ilooper = m_loopers.begin();

        while (ilooper != m_loopers.end() && target_is_available()) {
            auto looper = *ilooper;
            if (looper->get_paused()) {
                ilooper++;
                continue;
            }
            LooperFinishedStatus status = looper->loop_proc(debug_struct);

            switch (status) {
                case LooperFinishedPause:
                    looper->set_paused(true);
                    ilooper++;
                    break;
                case LooperFinishedStop:
                    looper->destroy();
                    ilooper = m_loopers.erase(ilooper);
                    break;
                case LooperFinishedStopAll:
                    clear_loopers();
                    m_cur_usecs = kEventLoopTimerDone;
                    return kEventLoopTimerDone;
                default:
                    ilooper++;
                    break;
            }
        }
        if (m_loopers.size() == 0) {
            m_cur_usecs = kEventLoopTimerDone;
            m_stopped = true;
        }
        return m_cur_usecs;
    } catch (LoopCableException e) {
        log.error("Loop manager cable error: exiting\n");
        return kEventLoopTimerDone;
    }
}

void LoopManager::set_loop_speed(bool fast) {
    if (m_stopped) return;
    log.detail("set loop speed fast %d\n", fast);
    m_cur_usecs = fast?m_fast_usecs:m_slow_usecs;
    m_loop_te->setTimeout(m_cur_usecs);
}

void LoopManager::start(bool fast) {
    log.debug("LoopManager started\n");
    for (auto &l : m_loopers) l->set_paused(false);
    m_stopped = false;
    set_loop_speed(fast);
}

void LoopManager::stop() {
    if (m_stopped) return;
    log.debug("LoopManager stopped\n");
    m_stopped = true;
    m_cur_usecs = kEventLoopTimerDone;
    m_loop_te->setTimeout(m_cur_usecs);
}

void LoopManager::add_looper(const std::shared_ptr<Looper> &looper) {
    m_loopers.emplace_back(std::move(looper));
    activate();
}

void LoopManager::remove_looper(Looper * looper) {
    for (auto li = m_loopers.begin(); li != m_loopers.end(); li++) {
        if (li->get() == looper) {
            looper->destroy();
            m_loopers.erase(li);
            return;
        }
    }
}

#ifdef __NEW_REQLOOP__
bool LoopManager::target_is_available()
{
#ifdef __CHECK_AVAILABILITY__
    if (m_target.available) return true;

    unsigned int value;
    if (!m_cable->jtag_get_reg(7, 4, &value, 0))
        throw LoopCableException();
    if (value & 2) {
        m_target.available = true;
        emit_availability_change(m_target.available);
        return true;
    }
    return false;
#else
    return true;
#endif
}

void LoopManager::target_state_sync(hal_target_state_t * target)
{
    bool availability_change = target->available != m_target.available;
    memcpy(&m_target, target, sizeof(m_target));
    if (availability_change) emit_availability_change(m_target.available);
}
#endif 

bool LoopManager::program_has_exited(hal_debug_struct_t *debug_struct)
{
    uint32_t value = 0;
    access(false, PTR_2_INT(&debug_struct->exit_status), 4, (char*)&value);
    if (value >> 31) {
      int32_t status = ((int32_t)value << 1) >> 1;
      log.user("Detected end of application, exiting with status: %d\n", status);
      emit_exit(status);
      return true;
    }
    return false;
}

hal_debug_struct_t * LoopManager::activate() {
    hal_debug_struct_t *debug_struct = NULL;

    access(false, m_debug_struct_addr, 4, (char*)&debug_struct);

    if (debug_struct != NULL) {
        auto ilooper = m_loopers.begin();
        while (ilooper != m_loopers.end()) {
            auto looper = *ilooper;
            if (looper->get_paused()) {
                ilooper++;
                continue;
            }

            LooperFinishedStatus status = looper->register_proc(debug_struct);

            switch (status) {
                case LooperFinishedPause:
                    looper->set_paused(true);
                    ilooper++;
                    break;
                case LooperFinishedStop:
                    looper->destroy();
                    ilooper = m_loopers.erase(ilooper);
                    break;
                case LooperFinishedStopAll:
                    clear_loopers();
                    m_cur_usecs = kEventLoopTimerDone;
                    return NULL;
                default:
                    ilooper++;
                    break;
            }
        }
    }

    return debug_struct;
}
