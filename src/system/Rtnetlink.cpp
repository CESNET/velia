/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <netlink/route/link.h>
#include <utility>
#include "Rtnetlink.h"
#include "utils/log.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

void nlCacheForeachWrapper(nl_cache* cache, std::function<void(rtnl_link*)> cb)
{
    nl_cache_foreach(
        cache, [](nl_object* obj, void* data) {
            auto& cb = *static_cast<std::function<void(rtnl_link*)>*>(data);
            auto link = reinterpret_cast<rtnl_link*>(obj);
            cb(link);
        },
        &cb);
}

void nlCacheMngrCallbackWrapper(struct nl_cache*, struct nl_object* obj, int action, void* data)
{
    auto objType = nl_object_get_type(obj);

    if (objType == "route/link"s) {
        auto* cb = static_cast<velia::system::Rtnetlink::LinkCB*>(data);
        (*cb)(reinterpret_cast<rtnl_link*>(obj), action);
    } else {
        throw velia::system::RtnetlinkException("Unknown netlink object type in cache: '"s + objType + "'");
    }
}

}

namespace velia::system {

namespace impl {

/** @brief Background thread watching for changes in netlink cache. Executes change callback from nl_cache_mngr_add. */
class nlCacheMngrWatcher {
    std::atomic<bool> m_terminate;
    std::thread m_thr;
    static constexpr auto FD_POLL_INTERVAL = 500ms;

    void run(velia::system::Rtnetlink::nlCacheManager manager);

public:
    nlCacheMngrWatcher(velia::system::Rtnetlink::nlCacheManager manager);
    ~nlCacheMngrWatcher();
};

nlCacheMngrWatcher::nlCacheMngrWatcher(velia::system::Rtnetlink::nlCacheManager manager)
    : m_terminate(false)
    , m_thr(&nlCacheMngrWatcher::run, this, manager)
{
}

void nlCacheMngrWatcher::run(velia::system::Rtnetlink::nlCacheManager manager)
{
    while (!m_terminate) {
        if (auto err = nl_cache_mngr_poll(manager.get(), std::chrono::duration_cast<std::chrono::milliseconds>(FD_POLL_INTERVAL).count()); err < 0) {
            throw velia::system::RtnetlinkException("nl_cache_mngr_poll", err);
        }
    }
}

nlCacheMngrWatcher::~nlCacheMngrWatcher()
{
    m_terminate = true;
    m_thr.join();
}

}

RtnetlinkException::RtnetlinkException(const std::string& msg)
    : std::runtime_error("Rtnetlink exception: " + msg)
{
}

RtnetlinkException::RtnetlinkException(const std::string& funcName, int error)
    : RtnetlinkException("Function '" + funcName + "' failed: " + nl_geterror(error))
{
}

Rtnetlink::Rtnetlink(LinkCB cbLink)
    : m_log(spdlog::get("system"))
    , m_cbLink(std::move(cbLink))
{
    {
        nl_cache_mngr* tmpManager;
        if (auto err = nl_cache_mngr_alloc(nullptr /* alloc and manage new netlink socket */, NETLINK_ROUTE, NL_AUTO_PROVIDE, &tmpManager); err < 0) {
            throw RtnetlinkException("nl_cache_mngr_alloc", err);
        }
        m_nlCacheManager = nlCacheManager(tmpManager, nl_cache_mngr_free);
    }

    // start listening for changes in cache manager in background thread
    // FIXME: implement event loop instead of nlCacheMngrWatcher, maybe with https://www.freedesktop.org/software/systemd/man/sd-event.html
    m_nlCacheMngrWatcher = std::make_unique<impl::nlCacheMngrWatcher>(m_nlCacheManager);

    nl_cache* cacheRouteLink;
    if (auto err = nl_cache_mngr_add(m_nlCacheManager.get(), "route/link", nlCacheMngrCallbackWrapper, &m_cbLink, &cacheRouteLink); err < 0) {
        throw RtnetlinkException("nl_cache_mngr_add", err);
    }

    // fire callbacks after getting the initial data into the cache.
    // populating the cache with nl_cache_mngr_add doesn't fire any cache change events
    nlCacheForeachWrapper(cacheRouteLink, [this](rtnl_link* link) {
        m_cbLink(link, NL_ACT_NEW);
    });
}

Rtnetlink::~Rtnetlink() = default;

}
