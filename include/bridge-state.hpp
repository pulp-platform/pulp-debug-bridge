#include "json.hpp"
#include "events/events.hpp"
#include "loops.hpp"
#include "gdb-server/gdb-server.hpp"
#include "cables/cable.hpp"
#include "cables/adv_dbg_itf/adv_dbg_itf.hpp"

#include <string>

class BridgeCommands;

class BridgeState {
    public:
        BridgeState(const char * config_string) {
            m_event_loop = EventLoop::getLoop();
            m_bridge_commands = std::make_shared<BridgeCommands>(this);
            m_system_config = js::import_config_from_string(std::string(config_string));
        }
        EventLoop::SpEventLoop m_event_loop;
        std::shared_ptr<Adv_dbg_itf> m_adu = nullptr;
        std::shared_ptr<Gdb_server> m_gdb_server = nullptr;
        std::shared_ptr<LoopManager> m_loop_manager = nullptr;
        std::shared_ptr<BridgeCommands> m_bridge_commands;
        std::shared_ptr<Ioloop> m_ioloop = nullptr;
        std::shared_ptr<Reqloop> m_reqloop = nullptr;
        js::config * m_system_config;
};