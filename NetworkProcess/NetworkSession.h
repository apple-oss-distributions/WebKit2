/*
 * Copyright (C) 2015-2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "PrefetchCache.h"
#include "SandboxExtension.h"
#include "WebResourceLoadStatisticsStore.h"
#include <WebCore/AdClickAttribution.h>
#include <WebCore/RegistrableDomain.h>
#include <pal/SessionID.h>
#include <wtf/HashSet.h>
#include <wtf/Ref.h>
#include <wtf/RefCounted.h>
#include <wtf/Seconds.h>
#include <wtf/UniqueRef.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class NetworkStorageSession;
class ResourceRequest;
enum class IncludeHttpOnlyCookies : bool;
enum class ShouldSample : bool;
}

namespace WebKit {

class AdClickAttributionManager;
class NetworkDataTask;
class NetworkProcess;
class NetworkResourceLoader;
class StorageManager;
class NetworkSocketChannel;
class WebResourceLoadStatisticsStore;
class WebSocketTask;
struct NetworkSessionCreationParameters;

enum class WebsiteDataType;
    
class NetworkSession : public RefCounted<NetworkSession>, public CanMakeWeakPtr<NetworkSession> {
public:
    static Ref<NetworkSession> create(NetworkProcess&, NetworkSessionCreationParameters&&);
    virtual ~NetworkSession();

    virtual void invalidateAndCancel();
    virtual void clearCredentials() { };
    virtual bool shouldLogCookieInformation() const { return false; }
    virtual Seconds loadThrottleLatency() const { return { }; }

    PAL::SessionID sessionID() const { return m_sessionID; }
    NetworkProcess& networkProcess() { return m_networkProcess; }
    WebCore::NetworkStorageSession* networkStorageSession() const;

    void registerNetworkDataTask(NetworkDataTask& task) { m_dataTaskSet.add(&task); }
    void unregisterNetworkDataTask(NetworkDataTask& task) { m_dataTaskSet.remove(&task); }

    StorageManager& storageManager() { return m_storageManager.get(); }

#if ENABLE(RESOURCE_LOAD_STATISTICS)
    WebResourceLoadStatisticsStore* resourceLoadStatistics() const { return m_resourceLoadStatistics.get(); }
    void setResourceLoadStatisticsEnabled(bool);
    bool isResourceLoadStatisticsEnabled() const;
    void notifyResourceLoadStatisticsProcessed();
    void deleteWebsiteDataForRegistrableDomains(OptionSet<WebsiteDataType>, HashMap<WebCore::RegistrableDomain, WebsiteDataToRemove>&&, bool shouldNotifyPage, CompletionHandler<void(const HashSet<WebCore::RegistrableDomain>&)>&&);
    void registrableDomainsWithWebsiteData(OptionSet<WebsiteDataType>, bool shouldNotifyPage, CompletionHandler<void(HashSet<WebCore::RegistrableDomain>&&)>&&);
    void logDiagnosticMessageWithValue(const String& message, const String& description, unsigned value, unsigned significantFigures, WebCore::ShouldSample);
    void notifyPageStatisticsTelemetryFinished(unsigned totalPrevalentResources, unsigned totalPrevalentResourcesWithUserInteraction, unsigned top3SubframeUnderTopFrameOrigins);
    void setShouldDowngradeReferrerForTesting(bool);
    bool shouldDowngradeReferrer() const;
#endif
    void storeAdClickAttribution(WebCore::AdClickAttribution&&);
    void handleAdClickAttributionConversion(WebCore::AdClickAttribution::Conversion&&, const URL& requestURL, const WebCore::ResourceRequest& redirectRequest);
    void dumpAdClickAttribution(CompletionHandler<void(String)>&&);
    void clearAdClickAttribution();
    void clearAdClickAttributionForRegistrableDomain(WebCore::RegistrableDomain&&);
    void setAdClickAttributionOverrideTimerForTesting(bool value);
    void setAdClickAttributionConversionURLForTesting(URL&&);
    void markAdClickAttributionsAsExpiredForTesting();

    void addKeptAliveLoad(Ref<NetworkResourceLoader>&&);
    void removeKeptAliveLoad(NetworkResourceLoader&);

    PrefetchCache& prefetchCache() { return m_prefetchCache; }
    void clearPrefetchCache() { m_prefetchCache.clear(); }

    virtual std::unique_ptr<WebSocketTask> createWebSocketTask(NetworkSocketChannel&, const WebCore::ResourceRequest&, const String& protocol);
    virtual void removeWebSocketTask(WebSocketTask&) { }
    virtual void addWebSocketTask(WebSocketTask&) { }

protected:
    NetworkSession(NetworkProcess&, PAL::SessionID, String&& localStorageDirectory, SandboxExtension::Handle&);

#if ENABLE(RESOURCE_LOAD_STATISTICS)
    void destroyResourceLoadStatistics();
#endif

    PAL::SessionID m_sessionID;
    Ref<NetworkProcess> m_networkProcess;
    HashSet<NetworkDataTask*> m_dataTaskSet;
    String m_resourceLoadStatisticsDirectory;
#if ENABLE(RESOURCE_LOAD_STATISTICS)
    RefPtr<WebResourceLoadStatisticsStore> m_resourceLoadStatistics;
    ShouldIncludeLocalhost m_shouldIncludeLocalhostInResourceLoadStatistics { ShouldIncludeLocalhost::Yes };
    EnableResourceLoadStatisticsDebugMode m_enableResourceLoadStatisticsDebugMode { EnableResourceLoadStatisticsDebugMode::No };
    WebCore::RegistrableDomain m_resourceLoadStatisticsManualPrevalentResource;
    bool m_downgradeReferrer { true };
#endif
    UniqueRef<AdClickAttributionManager> m_adClickAttribution;

    HashSet<Ref<NetworkResourceLoader>> m_keptAliveLoads;

    PrefetchCache m_prefetchCache;

    Ref<StorageManager> m_storageManager;
#if !ASSERT_DISABLED
    bool m_isInvalidated { false };
#endif
};

} // namespace WebKit
