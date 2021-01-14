#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <thread>
#include "system/RAUC.h"

/** @brief Mimics the RAUC DBus behaviour */
class DBusRAUCServer {
public:
    enum class InstallBehaviour {
        OK,
        FAILURE,
    };

    explicit DBusRAUCServer(sdbus::IConnection& connection, std::string primarySlot, const std::map<std::string, velia::system::RAUC::SlotProperties>& status);
    ~DBusRAUCServer();

    void installBundleBehaviour(InstallBehaviour b);

private:
    using DBusSlotStatus = sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>;

    void installBundle();
    void installBundleOK();
    void installBundleError();

    std::unique_ptr<sdbus::IObject> m_manager;
    std::string m_primarySlot;
    std::vector<DBusSlotStatus> m_status;
    std::string m_propOperation, m_propLastError;
    sdbus::Struct<int32_t, std::string, int32_t> m_propProgress;

    InstallBehaviour m_installBehaviour;

    std::thread m_installThread;
    std::mutex m_mtx;
    bool m_operationInProgress;
};
