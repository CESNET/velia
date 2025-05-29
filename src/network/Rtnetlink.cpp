/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <netlink/route/link.h>
#include <netlink/route/neighbour.h>
#include <utility>
#include "Rtnetlink.h"
#include "utils/log.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

template <class T>
void nlCacheForeachWrapper(nl_cache* cache, std::function<void(T*)> cb)
{
    nl_cache_foreach(
        cache, [](nl_object* obj, void* data) {
            auto& cb = *static_cast<std::function<void(T*)>*>(data);
            auto link = reinterpret_cast<T*>(obj);
            cb(link);
        },
        &cb);
}

void nlCacheMngrCallbackWrapper(struct nl_cache*, struct nl_object* obj, int action, void* data)
{
    auto objType = nl_object_get_type(obj);

    if (objType == "route/link"s) {
        auto* cb = static_cast<velia::network::Rtnetlink::LinkCB*>(data);
        (*cb)(reinterpret_cast<rtnl_link*>(obj), action);
    } else if (objType == "route/addr"s) {
        auto* cb = static_cast<velia::network::Rtnetlink::AddrCB*>(data);
        (*cb)(reinterpret_cast<rtnl_addr*>(obj), action);
    } else if (objType == "route/route"s) {
        auto* cb = static_cast<velia::network::Rtnetlink::RouteCB*>(data);
        (*cb)(reinterpret_cast<rtnl_route*>(obj), action);
    } else {
        throw velia::network::RtnetlinkException("Unknown netlink object type in cache: '"s + objType + "'");
    }
}

/** @brief Wraps rtnl object with unique_ptr and adds an appropriate deleter. */
template <class T>
std::unique_ptr<T, std::function<void(T*)>> nlObjectWrap(T* obj)
{
    return std::unique_ptr<T, std::function<void(T*)>>(obj, [] (T* obj) { nl_object_put(OBJ_CAST(obj)); });
}

template <class T>
T* nlObjectClone(T* obj)
{
    return reinterpret_cast<T*>(nl_object_clone(OBJ_CAST(obj)));
}

}

namespace velia::network {

namespace impl {

/** @brief Background thread watching for changes in netlink cache. Executes change callback from nl_cache_mngr_add. */
class nlCacheMngrWatcher {
    std::atomic<bool> m_terminate;
    std::thread m_thr;
    static constexpr auto FD_POLL_INTERVAL = 500ms;

    void run(velia::network::Rtnetlink::nlCacheManager manager);

public:
    nlCacheMngrWatcher(velia::network::Rtnetlink::nlCacheManager manager);
    ~nlCacheMngrWatcher();
};

nlCacheMngrWatcher::nlCacheMngrWatcher(velia::network::Rtnetlink::nlCacheManager manager)
    : m_terminate(false)
    , m_thr(&nlCacheMngrWatcher::run, this, manager)
{
}

void nlCacheMngrWatcher::run(velia::network::Rtnetlink::nlCacheManager manager)
{
    while (!m_terminate) {
        if (auto err = nl_cache_mngr_poll(manager.get(), std::chrono::duration_cast<std::chrono::milliseconds>(FD_POLL_INTERVAL).count()); err < 0) {
            throw velia::network::RtnetlinkException("nl_cache_mngr_poll", err);
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

Rtnetlink::Rtnetlink(LinkCB cbLink, AddrCB cbAddr, RouteCB cbRoute)
    : m_log(spdlog::get("network"))
    , m_nlSocket(nl_socket_alloc(), nl_socket_free)
    , m_cbLink(std::move(cbLink))
    , m_cbAddr(std::move(cbAddr))
    , m_cbRoute(std::move(cbRoute))
{
    if (!m_nlSocket) {
        throw RtnetlinkException("nl_socket_alloc failed");
    }

    if (auto err = nl_connect(m_nlSocket.get(), NETLINK_ROUTE); err < 0) {
        throw RtnetlinkException("nl_connect", err);
    }

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

    if (auto err = nl_cache_mngr_add(m_nlCacheManager.get(), "route/link", nlCacheMngrCallbackWrapper, &m_cbLink, &m_nlManagedCacheLink); err < 0) {
        throw RtnetlinkException("nl_cache_mngr_add", err);
    }

    if (auto err = nl_cache_mngr_add(m_nlCacheManager.get(), "route/addr", nlCacheMngrCallbackWrapper, &m_cbAddr, &m_nlManagedCacheAddr); err < 0) {
        throw RtnetlinkException("nl_cache_mngr_add", err);
    }

    if (auto err = nl_cache_mngr_add(m_nlCacheManager.get(), "route/route", nlCacheMngrCallbackWrapper, &m_cbRoute, &m_nlManagedCacheRoute); err < 0) {
        throw RtnetlinkException("nl_cache_mngr_add", err);
    }

    {
        nl_cache* tmpCache;

        if (auto err = rtnl_link_alloc_cache(m_nlSocket.get(), AF_UNSPEC, &tmpCache); err < 0) {
            throw RtnetlinkException("rtnl_link_alloc_cache", err);
        }

        m_nlCacheLink = nlCache(tmpCache, nl_cache_free);
    }

    {
        nl_cache* tmpCache;

        if (auto err = rtnl_neigh_alloc_cache(m_nlSocket.get(), &tmpCache); err < 0) {
            throw RtnetlinkException("rtnl_neigh_alloc_cache", err);
        }

        m_nlCacheNeighbour = nlCache(tmpCache, nl_cache_free);
    }

    {
        nl_cache* tmpCache;

        if (auto err = rtnl_route_alloc_cache(m_nlSocket.get(), AF_UNSPEC, ROUTE_CACHE_CONTENT, &tmpCache); err < 0) {
            throw RtnetlinkException("rtnl_route_alloc_cache", err);
        }

        m_nlCacheRoute = nlCache(tmpCache, nl_cache_free);
    }
}

Rtnetlink::~Rtnetlink() = default;

/* @brief Fire callbacks after getting the initial data into the cache; populating the cache with nl_cache_mngr_add doesn't fire any cache change events
 *
 * This code can't be in constructor because the callbacks can invoke other Rtnetlink methods while the instance is not yet constructed.
 */
void Rtnetlink::invokeInitialCallbacks()
{
    nlCacheForeachWrapper<rtnl_link>(m_nlManagedCacheLink, [this](rtnl_link* link) {
        m_cbLink(link, NL_ACT_NEW);
    });

    nlCacheForeachWrapper<rtnl_addr>(m_nlManagedCacheAddr, [this](rtnl_addr* addr) {
        m_cbAddr(addr, NL_ACT_NEW);
    });

    m_cbRoute(nullptr, NL_ACT_NEW); // when a single route is changed, we fetch all anyway, so no need to call update on all routes (as of now)
}

std::vector<Rtnetlink::nlLink> Rtnetlink::getLinks()
{
    resyncCache(m_nlCacheLink);

    std::vector<Rtnetlink::nlLink> res;
    nlCacheForeachWrapper<rtnl_link>(m_nlCacheLink.get(), [&res](rtnl_link* link) {
        res.emplace_back(nlObjectWrap(nlObjectClone(link)));
    });

    return res;
}

std::vector<std::pair<Rtnetlink::nlNeigh, Rtnetlink::nlLink>> Rtnetlink::getNeighbours()
{
    resyncCache(m_nlCacheLink);
    resyncCache(m_nlCacheNeighbour);

    std::vector<std::pair<Rtnetlink::nlNeigh, Rtnetlink::nlLink>> res;
    nlCacheForeachWrapper<rtnl_neigh>(m_nlCacheNeighbour.get(), [this, &res](rtnl_neigh* neigh) {
        auto link = rtnl_link_get(m_nlCacheLink.get(), rtnl_neigh_get_ifindex(neigh));

        res.emplace_back(nlObjectWrap(nlObjectClone(neigh)), nlObjectWrap(link));
    });

    return res;
}

std::vector<Rtnetlink::nlRoute> Rtnetlink::getRoutes()
{
#if 0
    resyncCache(m_nlCacheRoute); // first resync lowers the number of routes. It doesn't seem to recognize one particular route in ipv6. Possibly related to https://github.com/thom311/libnl/issues/224?
#else
    nl_cache* tmpCache;

    if (auto err = rtnl_route_alloc_cache(m_nlSocket.get(), AF_UNSPEC, 0, &tmpCache); err < 0) {
        throw RtnetlinkException("rtnl_route_alloc_cache", err);
    }

    m_nlCacheRoute = nlCache(tmpCache, nl_cache_free);
#endif

    std::vector<Rtnetlink::nlRoute> res;
    nlCacheForeachWrapper<rtnl_route>(m_nlCacheRoute.get(), [&res](rtnl_route* route) {
        res.emplace_back(nlObjectWrap(nlObjectClone(route)));
    });

    return res;
}

void Rtnetlink::resyncCache(const nlCache& cache)
{
    nl_cache_resync(m_nlSocket.get(), cache.get(), [](nl_cache*, nl_object*, int, void*){}, nullptr);
}
}
