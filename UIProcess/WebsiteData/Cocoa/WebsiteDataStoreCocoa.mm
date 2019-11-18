/*
 * Copyright (C) 2015-2018 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WebsiteDataStore.h"

#import "CookieStorageUtilsCF.h"
#import "StorageManager.h"
#import "WebResourceLoadStatisticsStore.h"
#import "WebsiteDataStoreParameters.h"
#import <WebCore/RegistrableDomain.h>
#import <WebCore/RuntimeApplicationChecks.h>
#import <WebCore/SearchPopupMenuCocoa.h>
#import <pal/spi/cf/CFNetworkSPI.h>
#import <wtf/FileSystem.h>
#import <wtf/NeverDestroyed.h>
#import <wtf/ProcessPrivilege.h>
#import <wtf/URL.h>
#import <wtf/text/StringBuilder.h>

#if PLATFORM(IOS_FAMILY)
#import <UIKit/UIApplication.h>
#endif

namespace WebKit {

static id terminationObserver;

static HashSet<WebsiteDataStore*>& dataStores()
{
    static NeverDestroyed<HashSet<WebsiteDataStore*>> dataStores;

    return dataStores;
}

static NSString * const WebKitNetworkLoadThrottleLatencyMillisecondsDefaultsKey = @"WebKitNetworkLoadThrottleLatencyMilliseconds";

WebsiteDataStoreParameters WebsiteDataStore::parameters()
{
    ASSERT(hasProcessPrivilege(ProcessPrivilege::CanAccessRawCookies));

    resolveDirectoriesIfNecessary();

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    bool shouldLogCookieInformation = false;
    bool enableResourceLoadStatisticsDebugMode = false;
    bool enableThirdPartyCookieBlockingOnSitesWithoutUserInteraction = true;
    WebCore::RegistrableDomain resourceLoadStatisticsManualPrevalentResource { };
#if ENABLE(RESOURCE_LOAD_STATISTICS)
    enableResourceLoadStatisticsDebugMode = [defaults boolForKey:@"ITPDebugMode"];
    if ([defaults boolForKey:@"InternalDisableThirdPartyCookieBlockingOnSitesWithoutUserInteraction"])
        enableThirdPartyCookieBlockingOnSitesWithoutUserInteraction = false;
    auto* manualPrevalentResource = [defaults stringForKey:@"ITPManualPrevalentResource"];
    if (manualPrevalentResource) {
        URL url { URL(), manualPrevalentResource };
        if (!url.isValid()) {
            StringBuilder builder;
            builder.appendLiteral("http://");
            builder.append(manualPrevalentResource);
            url = { URL(), builder.toString() };
        }
        if (url.isValid())
            resourceLoadStatisticsManualPrevalentResource = WebCore::RegistrableDomain { url };
    }
#if !RELEASE_LOG_DISABLED
    static NSString * const WebKitLogCookieInformationDefaultsKey = @"WebKitLogCookieInformation";
    shouldLogCookieInformation = [defaults boolForKey:WebKitLogCookieInformationDefaultsKey];
#endif
#endif // ENABLE(RESOURCE_LOAD_STATISTICS)

    URL httpProxy = m_configuration->httpProxy();
    URL httpsProxy = m_configuration->httpsProxy();
    
    bool isSafari = false;
#if PLATFORM(IOS_FAMILY)
    isSafari = WebCore::IOSApplication::isMobileSafari();
#elif PLATFORM(MAC)
    isSafari = WebCore::MacApplication::isSafari();
#endif
    // FIXME: Remove these once Safari adopts _WKWebsiteDataStoreConfiguration.httpProxy and .httpsProxy.
    if (!httpProxy.isValid() && isSafari)
        httpProxy = URL(URL(), [defaults stringForKey:(NSString *)WebKit2HTTPProxyDefaultsKey]);
    if (!httpsProxy.isValid() && isSafari)
        httpsProxy = URL(URL(), [defaults stringForKey:(NSString *)WebKit2HTTPSProxyDefaultsKey]);

    auto resourceLoadStatisticsDirectory = m_configuration->resourceLoadStatisticsDirectory();
    SandboxExtension::Handle resourceLoadStatisticsDirectoryHandle;
    if (!resourceLoadStatisticsDirectory.isEmpty())
        SandboxExtension::createHandleForReadWriteDirectory(resourceLoadStatisticsDirectory, resourceLoadStatisticsDirectoryHandle);

    auto localStorageDirectory = resolvedLocalStorageDirectory();
    SandboxExtension::Handle localStorageDirectoryExtensionHandle;
    if (!localStorageDirectory.isEmpty())
        SandboxExtension::createHandleForReadWriteDirectory(localStorageDirectory, localStorageDirectoryExtensionHandle);

    bool shouldIncludeLocalhostInResourceLoadStatistics = isSafari;
    WebsiteDataStoreParameters parameters;
    parameters.networkSessionParameters = {
        m_sessionID,
        m_boundInterfaceIdentifier,
        m_allowsCellularAccess,
        m_proxyConfiguration,
        m_sourceApplicationBundleIdentifier,
        m_sourceApplicationSecondaryIdentifier,
        m_allowsTLSFallback ? AllowsTLSFallback::Yes : AllowsTLSFallback::No,
        shouldLogCookieInformation,
        Seconds { [defaults integerForKey:WebKitNetworkLoadThrottleLatencyMillisecondsDefaultsKey] / 1000. },
        WTFMove(httpProxy),
        WTFMove(httpsProxy),
        WTFMove(resourceLoadStatisticsDirectory),
        WTFMove(resourceLoadStatisticsDirectoryHandle),
        false,
        shouldIncludeLocalhostInResourceLoadStatistics,
        enableResourceLoadStatisticsDebugMode,
        enableThirdPartyCookieBlockingOnSitesWithoutUserInteraction,
        m_configuration->deviceManagementRestrictionsEnabled(),
        m_configuration->allLoadsBlockedByDeviceManagementRestrictionsForTesting(),
        WTFMove(resourceLoadStatisticsManualPrevalentResource),
        WTFMove(localStorageDirectory),
        WTFMove(localStorageDirectoryExtensionHandle)
    };
    networkingHasBegun();

    auto cookieFile = resolvedCookieStorageFile();

    if (m_uiProcessCookieStorageIdentifier.isEmpty()) {
        auto utf8File = cookieFile.utf8();
        auto url = adoptCF(CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8 *)utf8File.data(), (CFIndex)utf8File.length(), true));
        m_cfCookieStorage = adoptCF(CFHTTPCookieStorageCreateFromFile(kCFAllocatorDefault, url.get(), nullptr));
        m_uiProcessCookieStorageIdentifier = identifyingDataFromCookieStorage(m_cfCookieStorage.get());
    }

    parameters.uiProcessCookieStorageIdentifier = m_uiProcessCookieStorageIdentifier;
    parameters.networkSessionParameters.sourceApplicationBundleIdentifier = m_sourceApplicationBundleIdentifier;
    parameters.networkSessionParameters.sourceApplicationSecondaryIdentifier = m_sourceApplicationSecondaryIdentifier;

    parameters.pendingCookies = copyToVector(m_pendingCookies);

    if (!cookieFile.isEmpty())
        SandboxExtension::createHandleForReadWriteDirectory(FileSystem::directoryName(cookieFile), parameters.cookieStoragePathExtensionHandle);

#if ENABLE(INDEXED_DATABASE)
    parameters.indexedDatabaseDirectory = resolvedIndexedDatabaseDirectory();
    if (!parameters.indexedDatabaseDirectory.isEmpty())
        SandboxExtension::createHandleForReadWriteDirectory(parameters.indexedDatabaseDirectory, parameters.indexedDatabaseDirectoryExtensionHandle);
#endif

#if ENABLE(SERVICE_WORKER)
    parameters.serviceWorkerRegistrationDirectory = resolvedServiceWorkerRegistrationDirectory();
    if (!parameters.serviceWorkerRegistrationDirectory.isEmpty())
        SandboxExtension::createHandleForReadWriteDirectory(parameters.serviceWorkerRegistrationDirectory, parameters.serviceWorkerRegistrationDirectoryExtensionHandle);
#endif

    parameters.perOriginStorageQuota = perOriginStorageQuota();
    parameters.perThirdPartyOriginStorageQuota = perThirdPartyOriginStorageQuota();

    return parameters;
}

void WebsiteDataStore::platformInitialize()
{
    if (!terminationObserver) {
        ASSERT(dataStores().isEmpty());

#if PLATFORM(MAC)
        NSString *notificationName = NSApplicationWillTerminateNotification;
#else
        NSString *notificationName = UIApplicationWillTerminateNotification;
#endif
        terminationObserver = [[NSNotificationCenter defaultCenter] addObserverForName:notificationName object:nil queue:nil usingBlock:^(NSNotification *note) {
            for (auto& dataStore : dataStores()) {
#if ENABLE(RESOURCE_LOAD_STATISTICS)
                if (dataStore->m_resourceLoadStatistics)
                    dataStore->m_resourceLoadStatistics->applicationWillTerminate();
#endif
            }
        }];
    }

    ASSERT(!dataStores().contains(this));
    dataStores().add(this);
}

void WebsiteDataStore::platformDestroy()
{
#if ENABLE(RESOURCE_LOAD_STATISTICS)
    if (m_resourceLoadStatistics)
        m_resourceLoadStatistics->applicationWillTerminate();
#endif

    ASSERT(dataStores().contains(this));
    dataStores().remove(this);

    if (dataStores().isEmpty()) {
        [[NSNotificationCenter defaultCenter] removeObserver:terminationObserver];
        terminationObserver = nil;
    }
}

void WebsiteDataStore::platformRemoveRecentSearches(WallTime oldestTimeToRemove)
{
    WebCore::removeRecentlyModifiedRecentSearches(oldestTimeToRemove);
}

}
