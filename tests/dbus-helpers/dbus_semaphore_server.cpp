#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include "dbus_semaphore_server.h"
#include "utils/log-init.h"

/* Ask for current value:
 * dbus-send --print-reply --system --dest=<bus> /cz/cesnet/led org.freedesktop.DBus.Properties.Get string:cz.cesnet.Led string:semaphore
 */

DbusSemaphoreServer::DbusSemaphoreServer(sdbus::IConnection& connection, const std::string& objectPath, const std::string& propertyName, const std::string& propertyInterface, const std::string& state)
    : m_object(sdbus::createObject(connection, objectPath))
    , m_propertyName(propertyName)
    , m_propertyInterface(propertyInterface)
    , m_semaphoreState(state)
{
    // Register D-Bus methods and signals on the object, and exports the object.
    m_object->registerProperty(m_propertyName).onInterface(m_propertyInterface).withGetter([&]() {
        std::lock_guard<std::mutex> lock(m_semaphoreStateMtx);
        return m_semaphoreState;
    });
    m_object->finishRegistration();
}

void DbusSemaphoreServer::runStateChanges(const std::vector<std::pair<std::string, std::chrono::milliseconds>>& sequence)
{
    std::thread serverThr([&]() {
        for (const auto& [state, sleepTime] : sequence) {
            {
                std::lock_guard<std::mutex> lock(m_semaphoreStateMtx);
                m_semaphoreState = state;
            }
            m_object->emitPropertiesChangedSignal(m_propertyInterface, {m_propertyName});
            std::this_thread::sleep_for(sleepTime);
        }
    });

    serverThr.join();
}