#include <random>
#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include "dbus-semaphore_server.h"
#include "utils/log-init.h"

/* Ask for current value:
 * dbus-send --print-reply --system --dest=cz.cesnet.led /cz/cesnet/led org.freedesktop.DBus.Properties.Get string:cz.cesnet.Led string:semaphore
 */

DbusSemaphoreServer::DbusSemaphoreServer(const std::string& serviceName, const std::string& objectPath, const std::string& propertyInterface)
    : m_connection(sdbus::createSystemBusConnection(serviceName))
    , m_object(sdbus::createObject(*m_connection, objectPath))
    , m_propertyInterface(propertyInterface)
{
    // Register D-Bus methods and signals on the object, and exports the object.
    m_object->registerProperty("Semaphore").onInterface(m_propertyInterface).withGetter([&]() {
        std::lock_guard<std::mutex> lock(m_semaphoreStateMtx);
        return m_semaphoreState;
    });
    m_object->finishRegistration();
}

void DbusSemaphoreServer::run(const std::vector<std::string>& sequence)
{
    std::thread thr1([&]() { runInternal(sequence); });
    m_connection->enterEventLoop();

    thr1.join();
}

void DbusSemaphoreServer::runInternal(const std::vector<std::string>& sequence)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(0, 555);

    for (const auto& state : sequence) {
        {
            std::lock_guard<std::mutex> lock(m_semaphoreStateMtx);
            m_semaphoreState = state;
        }
        m_object->emitPropertiesChangedSignal(m_propertyInterface, {"Semaphore"});
        std::this_thread::sleep_for(std::chrono::milliseconds {distr(gen)});
    }

    m_connection->leaveEventLoop();
}
