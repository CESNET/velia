#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include "dbus_semaphore_server.h"
#include "utils/log-init.h"

/* Ask for current value:
 * dbus-send --print-reply --system --dest=cz.cesnet.led /cz/cesnet/led org.freedesktop.DBus.Properties.Get string:cz.cesnet.Led string:semaphore
 */

DbusSemaphoreServer::DbusSemaphoreServer(const std::string& serviceName, const std::string& objectPath, const std::string& propertyName, const std::string& propertyInterface)
    : m_connection(sdbus::createSystemBusConnection(serviceName))
    , m_object(sdbus::createObject(*m_connection, objectPath))
    , m_propertyName(propertyName)
    , m_propertyInterface(propertyInterface)
{
    // Register D-Bus methods and signals on the object, and exports the object.
    m_object->registerProperty(m_propertyName).onInterface(m_propertyInterface).withGetter([&]() {
        std::lock_guard<std::mutex> lock(m_semaphoreStateMtx);
        return m_semaphoreState;
    });
    m_object->finishRegistration();
}

void DbusSemaphoreServer::run(const std::vector<std::pair<std::string, std::chrono::milliseconds>>& sequence)
{
    std::thread serverThr([&]() { runServer(sequence); });

    // enter dbus event loop
    // the event loop is terminated at the end of runServer method
    m_connection->enterEventLoop();

    serverThr.join();
}

void DbusSemaphoreServer::runServer(const std::vector<std::pair<std::string, std::chrono::milliseconds>>& sequence)
{
    for (const auto& [state, sleepTime] : sequence) {
        {
            std::lock_guard<std::mutex> lock(m_semaphoreStateMtx);
            m_semaphoreState = state;
        }
        m_object->emitPropertiesChangedSignal(m_propertyInterface, {m_propertyName});

        // wait for a while
        if (sleepTime != std::chrono::milliseconds().zero()) {
            std::this_thread::sleep_for(sleepTime);
        }
    }

    m_connection->leaveEventLoop();
}
