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

#if 0
[[maybe_unused]]
void nlCacheForeachWrapper(std::unique_ptr<nl_cache, std::function<void(nl_cache*)>>& cache, std::function<void(rtnl_link*)> cb)
{
    nl_cache_foreach(
        cache.get(), [](nl_object* obj, void* data) {
            auto& cb = *static_cast<std::function<void(rtnl_link*)>*>(data);
            auto link = reinterpret_cast<rtnl_link*>(obj);
            cb(link);
        },
        &cb);
}
#endif

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

const auto NETLINK_FD_POLL_INTERVAL = 500ms;

}

namespace velia::system {

RtnetlinkException::RtnetlinkException(const std::string& funcName)
    : std::runtime_error("Rtnetlink call " + funcName + " failed.")
{
}

RtnetlinkException::RtnetlinkException(const std::string& funcName, int error)
    : std::runtime_error("Rtnetlink call " + funcName + " failed: " + nl_geterror(error))
{
}

Rtnetlink::Rtnetlink(std::function<void(rtnl_link*, int)> cbLink)
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

    // start listening for changes in cache manager
    // FIXME: implement event loop, maybe with https://www.freedesktop.org/software/systemd/man/sd-event.html
    m_changes.m_thr = std::thread([this]() {
        try {
            while (!m_changes.shouldTerminate()) {
                if (auto err = nl_cache_mngr_poll(m_nlCacheManager.get(), std::chrono::duration_cast<std::chrono::milliseconds>(NETLINK_FD_POLL_INTERVAL).count()); err < 0) {
                    throw RtnetlinkException("nl_cache_mngr_poll", err);
                }
            }
        } catch (const RtnetlinkException& e) {
            m_log->error("Netlink changes thread: {}", e.what());
        }
    });

    // populate cache from route/link, setup change callback
    nl_cache* tmpCache;
    if (auto err = nl_cache_mngr_add(
            m_nlCacheManager.get(),
            "route/link",
            [](struct nl_cache*, struct nl_object* obj, int action, void* data) {
                auto* d = static_cast<Rtnetlink*>(data);

                auto objType = nl_object_get_type(obj);
                d->m_log->trace("Data in cache manager changed! Invoking callback for {} cache.", objType);

                if (objType == "route/link"s) {
                    d->m_cbLink(reinterpret_cast<rtnl_link*>(obj), action);
                } else {
                    throw velia::system::RtnetlinkException("Unknown object type '"s + objType + "' in cache");
                }
            },
            this,
            &tmpCache);
        err < 0) {
        throw RtnetlinkException("nl_cache_mngr_add", err);
    }

    // run callbacks with initial data, because populating the cache with nl_cache_mngr_add doesn't fire any cache change events
    nlCacheForeachWrapper(tmpCache, [this](rtnl_link* link) {
        m_cbLink(link, NL_ACT_NEW);
    });
}

Rtnetlink::~Rtnetlink()
{
    m_changes.join();
}

bool Rtnetlink::nlCacheManagerChangesThread::shouldTerminate()
{
    std::lock_guard<std::mutex> lck(m_mtx);
    return m_terminate;
}

void Rtnetlink::nlCacheManagerChangesThread::join()
{
    {
        std::lock_guard<std::mutex> lck(m_mtx);
        m_terminate = true;
    }

    m_thr.join();
}

}
