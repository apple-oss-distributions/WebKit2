/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

#if ENABLE(SERVICE_WORKER)

#include "WebSWServerConnection.h"
#include <WebCore/FetchIdentifier.h>
#include <WebCore/ServiceWorkerTypes.h>
#include <WebCore/Timer.h>
#include <pal/SessionID.h>
#include <wtf/RefCounted.h>

namespace WebCore {
class ResourceError;
class ResourceResponse;
}

namespace IPC {
class Connection;
class DataReference;
class Decoder;
class FormDataReference;
}

namespace WebKit {

class WebSWServerToContextConnection;

class ServiceWorkerFetchTask : public RefCounted<ServiceWorkerFetchTask> {
public:
    template<typename... Args> static Ref<ServiceWorkerFetchTask> create(Args&&... args)
    {
        return adoptRef(*new ServiceWorkerFetchTask(std::forward<Args>(args)...));
    }

    void didNotHandle();
    void fail(const WebCore::ResourceError& error) { didFail(error); }
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);

    struct Identifier {
        WebCore::SWServerConnectionIdentifier connectionIdentifier;
        WebCore::FetchIdentifier fetchIdentifier;
        
        unsigned hash() const
        {
            unsigned hashes[2];
            hashes[0] = WTF::intHash(connectionIdentifier.toUInt64());
            hashes[1] = WTF::intHash(fetchIdentifier.toUInt64());
            return StringHasher::hashMemory(hashes, sizeof(hashes));
        }
    };

    const Identifier& identifier() const { return m_identifier; }
    const WebCore::ServiceWorkerIdentifier& serviceWorkerIdentifier() const { return m_serviceWorkerIdentifier; }
    bool wasHandled() const { return m_wasHandled; }

    WebSWServerConnection* swServerConnection() { return m_connection.get(); }

private:
    ServiceWorkerFetchTask(PAL::SessionID, WebSWServerConnection&, WebSWServerToContextConnection&, WebCore::FetchIdentifier, WebCore::ServiceWorkerIdentifier, Seconds timeout);

    void didReceiveRedirectResponse(const WebCore::ResourceResponse&);
    void didReceiveResponse(const WebCore::ResourceResponse&, bool needsContinueDidReceiveResponseMessage);
    void didReceiveData(const IPC::DataReference&, int64_t encodedDataLength);
    void didReceiveFormData(const IPC::FormDataReference&);
    void didFinish();
    void didFail(const WebCore::ResourceError&);
    void timeoutTimerFired();

    PAL::SessionID m_sessionID;
    WeakPtr<WebSWServerConnection> m_connection;
    WebSWServerToContextConnection& m_contextConnection;
    Identifier m_identifier;
    WebCore::ServiceWorkerIdentifier m_serviceWorkerIdentifier;
    Seconds m_timeout;
    WebCore::Timer m_timeoutTimer;
    bool m_wasHandled { false };
    bool m_didReachTerminalState { false };
};

inline bool operator==(const ServiceWorkerFetchTask::Identifier& a, const ServiceWorkerFetchTask::Identifier& b)
{
    return a.connectionIdentifier == b.connectionIdentifier &&  a.fetchIdentifier == b.fetchIdentifier;
}

} // namespace WebKit


namespace WTF {

struct ServiceWorkerFetchTaskIdentifierHash {
    static unsigned hash(const WebKit::ServiceWorkerFetchTask::Identifier& key) { return key.hash(); }
    static bool equal(const WebKit::ServiceWorkerFetchTask::Identifier& a, const WebKit::ServiceWorkerFetchTask::Identifier& b) { return a == b; }
    static const bool safeToCompareToEmptyOrDeleted = true;
};

template<> struct HashTraits<WebKit::ServiceWorkerFetchTask::Identifier> : GenericHashTraits<WebKit::ServiceWorkerFetchTask::Identifier> {
    static WebKit::ServiceWorkerFetchTask::Identifier emptyValue() { return { }; }
    
    static void constructDeletedValue(WebKit::ServiceWorkerFetchTask::Identifier& slot) { slot.connectionIdentifier = makeObjectIdentifier<WebCore::SWServerConnectionIdentifierType>(std::numeric_limits<uint64_t>::max()); }
    
    static bool isDeletedValue(const WebKit::ServiceWorkerFetchTask::Identifier& slot) { return slot.connectionIdentifier.toUInt64() == std::numeric_limits<uint64_t>::max(); }
};

template<> struct DefaultHash<WebKit::ServiceWorkerFetchTask::Identifier> {
    using Hash = ServiceWorkerFetchTaskIdentifierHash;
};

}

#endif // ENABLE(SERVICE_WORKER)
