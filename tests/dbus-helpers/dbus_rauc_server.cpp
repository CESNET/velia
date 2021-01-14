#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include "dbus_rauc_server.h"
#include "utils/log-init.h"

#include "utils/log.h"

using namespace std::literals;

namespace {
const std::string interfaceManager = "de.pengutronix.rauc.Installer";
const std::string objectPathManager = "/";
}

#define PROGRESS(perc,msg,depth)                                           \
    m_propProgress = sdbus::make_struct(perc, msg, depth);                 \
    m_manager->emitPropertiesChangedSignal(interfaceManager, {"Progress"});

#define OPERATION(op)     \
    m_propOperation = op; \
    m_manager->emitPropertiesChangedSignal(interfaceManager, {"Operation"});

#define LAST_ERROR(msg)     \
    m_propLastError = msg; \
    m_manager->emitPropertiesChangedSignal(interfaceManager, {"LastError"});

DBusRAUCServer::~DBusRAUCServer()
{
    if (m_installThread.joinable())
        m_installThread.join();
}

/** @brief Create a dbus server on the connection */
DBusRAUCServer::DBusRAUCServer(sdbus::IConnection& connection, std::string primarySlot, const std::map<std::string, velia::system::RAUC::SlotProperties>& status)
    : m_manager(sdbus::createObject(connection, objectPathManager))
    , m_primarySlot(std::move(primarySlot))
    , m_installInProgress(false)
{
    for (const auto& [slotName, slotStatus] : status) {
        std::map<std::string, sdbus::Variant> m;
        for (auto it = slotStatus.begin(); it != slotStatus.end(); ++it) {
            // NOTE: I wanted a for-range loop over the map with structured binding [key, val] but this did not compile with clang++.
            // According to http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0588r1.html:
            //  "If a lambda-expression [...] captures a structured binding (explicitly or implicitly), the program is ill-formed."

            const auto& k = it->first;
            std::visit([&k, &m](auto&& arg) { m.insert(std::make_pair(k, sdbus::Variant(arg))); }, it->second);
        }
        m_status.emplace_back(slotName, m);
    }

    // create manager object
    m_manager->registerMethod("GetSlotStatus").onInterface(interfaceManager).implementedAs([this]() { return m_status; });
    m_manager->registerMethod("GetPrimary").onInterface(interfaceManager).implementedAs([this]() { return m_primarySlot; });
    m_manager->registerMethod("InstallBundle").onInterface(interfaceManager).implementedAs(
        [this]([[maybe_unused]] const std::string& source)
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_installInProgress) {
                m_installInProgress = true;
                m_installThread = std::thread(&DBusRAUCServer::InstallBundleSuccessful, this);
            } else {
                throw sdbus::Error("org.gtk.GDBus.UnmappedGError.Quark._g_2dio_2derror_2dquark.Code30", "Already processing a different method");
            }
        });
    m_manager->registerProperty("Operation").onInterface(interfaceManager).withGetter([this](){ return m_propOperation; });
    m_manager->registerProperty("LastError").onInterface(interfaceManager).withGetter([this](){ return m_propLastError; });
    m_manager->registerProperty("Progress").onInterface(interfaceManager).withGetter([this](){ return m_propProgress; });
    m_manager->registerSignal("Completed").onInterface(interfaceManager).withParameters<int32_t>();
    m_manager->finishRegistration();
}

/** @brief Mimics behaviour of RAUC's InstallBundle DBus method
 *
 *  The behaviour was reverse-engineered from real device by bus monitoring ('busctl monitor de.pengutronix.rauc')
 *  when issuing 'busctl call de.pengutronix.rauc / de.pengutronix.rauc.Installer InstallBundle sa{sv} "/path/to/source" 0'
 * */
void DBusRAUCServer::InstallBundleSuccessful()
{
    OPERATION("installing");

    m_propLastError = "";
    m_propProgress = sdbus::make_struct(0, "Installing", 1);
    m_manager->emitPropertiesChangedSignal(interfaceManager, {"LastError", "Progress"});

    PROGRESS(0, "Determining slot states", 2);
    PROGRESS(20, "Determining slot states done.", 2);
    PROGRESS(20, "Checking bundle", 2);
    PROGRESS(20, "Veryfing signature", 3);
    PROGRESS(40, "Veryfing signature done.", 3);
    PROGRESS(40, "Checking bundle done.", 2);
    PROGRESS(40, "Loading manifest file", 2);
    PROGRESS(60, "Loading manifest file done.", 2);
    PROGRESS(60, "Determining target install group", 2);
    PROGRESS(80, "Determining target install group done.", 2);
    PROGRESS(80, "Updating slots", 2);
    PROGRESS(80, "Checking slot rootfs.0", 3);
    PROGRESS(85, "Checking slot rootfs.0 done.", 3);
    PROGRESS(85, "Copying image to rootfs.0", 3);
    PROGRESS(90, "Copying image to rootfs.0 done.", 3);
    PROGRESS(90, "Checking slot cfg.0", 3);
    PROGRESS(95, "Checking slot cfg.0 done.", 3);
    PROGRESS(95, "Copying image to cfg.0", 3);
    PROGRESS(100, "Copying image to cfg.0 done.", 3);
    PROGRESS(100, "Updating slots done.", 2);
    PROGRESS(100, "Installing done.", 1);

    m_manager->emitSignal("Completed").onInterface(interfaceManager).withArguments(0);

    OPERATION("idle");

    std::lock_guard<std::mutex> lock(m_mtx);
    m_installInProgress = false;
}

void DBusRAUCServer::InstallBundleError()
{
    OPERATION("installing");

    m_propLastError = "";
    m_propProgress = sdbus::make_struct(0, "Installing", 1);
    m_manager->emitPropertiesChangedSignal(interfaceManager, {"LastError", "Progress"});

    PROGRESS(0, "Installing", 1);
    PROGRESS(0, "Determining slot states", 2);
    PROGRESS(20, "Determining slot states done.", 2);
    PROGRESS(20, "Checking bundle", 2);
    PROGRESS(40, "Checking bundle failed.", 2);
    PROGRESS(100, "Installing failed.", 1);

    LAST_ERROR("Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number");

    m_manager->emitSignal("Completed").onInterface(interfaceManager).withArguments(1);

    OPERATION("idle");

    std::lock_guard<std::mutex> lock(m_mtx);
    m_installInProgress = false;
}
