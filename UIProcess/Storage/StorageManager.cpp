/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

#include "config.h"
#include "StorageManager.h"

#include "LocalStorageDatabase.h"
#include "LocalStorageDatabaseTracker.h"
#include "LocalStorageDetails.h"
#include "SecurityOriginData.h"
#include "StorageAreaMapMessages.h"
#include "StorageManagerMessages.h"
#include "WebProcessProxy.h"
#include "WorkQueue.h"
#include <WebCore/SecurityOriginHash.h>
#include <WebCore/StorageMap.h>
#include <WebCore/TextEncoding.h>
#include <memory>
#include <wtf/threads/BinarySemaphore.h>

using namespace WebCore;

namespace WebKit {

class StorageManager::StorageArea : public ThreadSafeRefCounted<StorageManager::StorageArea> {
public:
    static PassRefPtr<StorageArea> create(LocalStorageNamespace*, PassRefPtr<SecurityOrigin>, unsigned quotaInBytes);
    ~StorageArea();

    SecurityOrigin* securityOrigin() const { return m_securityOrigin.get(); }

    void addListener(IPC::Connection*, uint64_t storageMapID);
    void removeListener(IPC::Connection*, uint64_t storageMapID);

    PassRefPtr<StorageArea> clone() const;

    void setItem(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& key, const String& value, const String& urlString, bool& quotaException);
    void removeItem(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& key, const String& urlString);
    void clear(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& urlString);

    const HashMap<String, String>& items();
    void clear();

private:
    explicit StorageArea(LocalStorageNamespace*, PassRefPtr<SecurityOrigin>, unsigned quotaInBytes);

    void openDatabaseAndImportItemsIfNeeded();

    void dispatchEvents(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& key, const String& oldValue, const String& newValue, const String& urlString) const;

    // Will be null if the storage area belongs to a session storage namespace.
    LocalStorageNamespace* m_localStorageNamespace;
    RefPtr<LocalStorageDatabase> m_localStorageDatabase;
    bool m_didImportItemsFromDatabase;

    RefPtr<SecurityOrigin> m_securityOrigin;
    unsigned m_quotaInBytes;

    RefPtr<StorageMap> m_storageMap;
    HashSet<std::pair<RefPtr<IPC::Connection>, uint64_t>> m_eventListeners;
};

class StorageManager::LocalStorageNamespace : public ThreadSafeRefCounted<LocalStorageNamespace> {
public:
    static PassRefPtr<LocalStorageNamespace> create(StorageManager*, uint64_t storageManagerID);
    ~LocalStorageNamespace();

    StorageManager* storageManager() const { return m_storageManager; }

    PassRefPtr<StorageArea> getOrCreateStorageArea(PassRefPtr<SecurityOrigin>);
    void didDestroyStorageArea(StorageArea*);

    void clearStorageAreasMatchingOrigin(SecurityOrigin*);
    void clearAllStorageAreas();

private:
    explicit LocalStorageNamespace(StorageManager*, uint64_t storageManagerID);

    StorageManager* m_storageManager;
    uint64_t m_storageNamespaceID;
    unsigned m_quotaInBytes;

    // We don't hold an explicit reference to the StorageAreas; they are kept alive by the m_storageAreasByConnection map in StorageManager.
    HashMap<RefPtr<SecurityOrigin>, StorageArea*> m_storageAreaMap;
};

PassRefPtr<StorageManager::StorageArea> StorageManager::StorageArea::create(LocalStorageNamespace* localStorageNamespace, PassRefPtr<SecurityOrigin> securityOrigin, unsigned quotaInBytes)
{
    return adoptRef(new StorageArea(localStorageNamespace, securityOrigin, quotaInBytes));
}

StorageManager::StorageArea::StorageArea(LocalStorageNamespace* localStorageNamespace, PassRefPtr<SecurityOrigin> securityOrigin, unsigned quotaInBytes)
    : m_localStorageNamespace(localStorageNamespace)
    , m_didImportItemsFromDatabase(false)
    , m_securityOrigin(securityOrigin)
    , m_quotaInBytes(quotaInBytes)
    , m_storageMap(StorageMap::create(m_quotaInBytes))
{
}

StorageManager::StorageArea::~StorageArea()
{
    ASSERT(m_eventListeners.isEmpty());

    if (m_localStorageDatabase)
        m_localStorageDatabase->close();

    if (m_localStorageNamespace)
        m_localStorageNamespace->didDestroyStorageArea(this);
}

void StorageManager::StorageArea::addListener(IPC::Connection* connection, uint64_t storageMapID)
{
    ASSERT(!m_eventListeners.contains(std::make_pair(connection, storageMapID)));
    m_eventListeners.add(std::make_pair(connection, storageMapID));
}

void StorageManager::StorageArea::removeListener(IPC::Connection* connection, uint64_t storageMapID)
{
    ASSERT(m_eventListeners.contains(std::make_pair(connection, storageMapID)));
    m_eventListeners.remove(std::make_pair(connection, storageMapID));
}

PassRefPtr<StorageManager::StorageArea> StorageManager::StorageArea::clone() const
{
    ASSERT(!m_localStorageNamespace);

    RefPtr<StorageArea> storageArea = StorageArea::create(0, m_securityOrigin, m_quotaInBytes);
    storageArea->m_storageMap = m_storageMap;

    return storageArea.release();
}

void StorageManager::StorageArea::setItem(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& key, const String& value, const String& urlString, bool& quotaException)
{
    openDatabaseAndImportItemsIfNeeded();

    String oldValue;

    RefPtr<StorageMap> newStorageMap = m_storageMap->setItem(key, value, oldValue, quotaException);
    if (newStorageMap)
        m_storageMap = newStorageMap.release();

    if (quotaException)
        return;

    if (m_localStorageDatabase)
        m_localStorageDatabase->setItem(key, value);

    dispatchEvents(sourceConnection, sourceStorageAreaID, key, oldValue, value, urlString);
}

void StorageManager::StorageArea::removeItem(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& key, const String& urlString)
{
    openDatabaseAndImportItemsIfNeeded();

    String oldValue;
    RefPtr<StorageMap> newStorageMap = m_storageMap->removeItem(key, oldValue);
    if (newStorageMap)
        m_storageMap = newStorageMap.release();

    if (oldValue.isNull())
        return;

    if (m_localStorageDatabase)
        m_localStorageDatabase->removeItem(key);

    dispatchEvents(sourceConnection, sourceStorageAreaID, key, oldValue, String(), urlString);
}

void StorageManager::StorageArea::clear(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& urlString)
{
    openDatabaseAndImportItemsIfNeeded();

    if (!m_storageMap->length())
        return;

    m_storageMap = StorageMap::create(m_quotaInBytes);

    if (m_localStorageDatabase)
        m_localStorageDatabase->clear();

    dispatchEvents(sourceConnection, sourceStorageAreaID, String(), String(), String(), urlString);
}

const HashMap<String, String>& StorageManager::StorageArea::items()
{
    openDatabaseAndImportItemsIfNeeded();

    return m_storageMap->items();
}

void StorageManager::StorageArea::clear()
{
    m_storageMap = StorageMap::create(m_quotaInBytes);

    if (m_localStorageDatabase) {
        m_localStorageDatabase->close();
        m_localStorageDatabase = nullptr;
    }

    for (auto it = m_eventListeners.begin(), end = m_eventListeners.end(); it != end; ++it)
        it->first->send(Messages::StorageAreaMap::ClearCache(), it->second);
}

void StorageManager::StorageArea::openDatabaseAndImportItemsIfNeeded()
{
    if (!m_localStorageNamespace)
        return;

    // We open the database here even if we've already imported our items to ensure that the database is open if we need to write to it.
    if (!m_localStorageDatabase)
        m_localStorageDatabase = LocalStorageDatabase::create(m_localStorageNamespace->storageManager()->m_queue, m_localStorageNamespace->storageManager()->m_localStorageDatabaseTracker, m_securityOrigin.get());

    if (m_didImportItemsFromDatabase)
        return;

    m_localStorageDatabase->importItems(*m_storageMap);
    m_didImportItemsFromDatabase = true;
}

void StorageManager::StorageArea::dispatchEvents(IPC::Connection* sourceConnection, uint64_t sourceStorageAreaID, const String& key, const String& oldValue, const String& newValue, const String& urlString) const
{
    for (HashSet<std::pair<RefPtr<IPC::Connection>, uint64_t>>::const_iterator it = m_eventListeners.begin(), end = m_eventListeners.end(); it != end; ++it) {
        uint64_t storageAreaID = it->first == sourceConnection ? sourceStorageAreaID : 0;

        it->first->send(Messages::StorageAreaMap::DispatchStorageEvent(storageAreaID, key, oldValue, newValue, urlString), it->second);
    }
}

PassRefPtr<StorageManager::LocalStorageNamespace> StorageManager::LocalStorageNamespace::create(StorageManager* storageManager, uint64_t storageNamespaceID)
{
    return adoptRef(new LocalStorageNamespace(storageManager, storageNamespaceID));
}

// FIXME: The quota value is copied from GroupSettings.cpp.
// We should investigate a way to share it with WebCore.
StorageManager::LocalStorageNamespace::LocalStorageNamespace(StorageManager* storageManager, uint64_t storageNamespaceID)
    : m_storageManager(storageManager)
    , m_storageNamespaceID(storageNamespaceID)
    , m_quotaInBytes(5 * 1024 * 1024)
{
}

StorageManager::LocalStorageNamespace::~LocalStorageNamespace()
{
    ASSERT(m_storageAreaMap.isEmpty());
}

PassRefPtr<StorageManager::StorageArea> StorageManager::LocalStorageNamespace::getOrCreateStorageArea(PassRefPtr<SecurityOrigin> securityOrigin)
{
    auto result = m_storageAreaMap.add(securityOrigin, nullptr);
    if (!result.isNewEntry)
        return result.iterator->value;

    RefPtr<StorageArea> storageArea = StorageArea::create(this, result.iterator->key, m_quotaInBytes);
    result.iterator->value = storageArea.get();

    return storageArea.release();
}

void StorageManager::LocalStorageNamespace::didDestroyStorageArea(StorageArea* storageArea)
{
    ASSERT(m_storageAreaMap.contains(storageArea->securityOrigin()));

    m_storageAreaMap.remove(storageArea->securityOrigin());
    if (!m_storageAreaMap.isEmpty())
        return;

    ASSERT(m_storageManager->m_localStorageNamespaces.contains(m_storageNamespaceID));
    m_storageManager->m_localStorageNamespaces.remove(m_storageNamespaceID);
}

void StorageManager::LocalStorageNamespace::clearStorageAreasMatchingOrigin(SecurityOrigin* securityOrigin)
{
    for (auto it = m_storageAreaMap.begin(), end = m_storageAreaMap.end(); it != end; ++it) {
        if (it->key->equal(securityOrigin))
            it->value->clear();
    }
}

void StorageManager::LocalStorageNamespace::clearAllStorageAreas()
{
    for (auto it = m_storageAreaMap.begin(), end = m_storageAreaMap.end(); it != end; ++it)
        it->value->clear();
}

class StorageManager::SessionStorageNamespace : public ThreadSafeRefCounted<SessionStorageNamespace> {
public:
    static PassRefPtr<SessionStorageNamespace> create(IPC::Connection* allowedConnection, unsigned quotaInBytes);
    ~SessionStorageNamespace();

    bool isEmpty() const { return m_storageAreaMap.isEmpty(); }

    IPC::Connection* allowedConnection() const { return m_allowedConnection.get(); }
    void setAllowedConnection(IPC::Connection*);

    PassRefPtr<StorageArea> getOrCreateStorageArea(PassRefPtr<SecurityOrigin>);

    void cloneTo(SessionStorageNamespace& newSessionStorageNamespace);

private:
    SessionStorageNamespace(IPC::Connection* allowedConnection, unsigned quotaInBytes);

    RefPtr<IPC::Connection> m_allowedConnection;
    unsigned m_quotaInBytes;

    HashMap<RefPtr<SecurityOrigin>, RefPtr<StorageArea>> m_storageAreaMap;
};

PassRefPtr<StorageManager::SessionStorageNamespace> StorageManager::SessionStorageNamespace::create(IPC::Connection* allowedConnection, unsigned quotaInBytes)
{
    return adoptRef(new SessionStorageNamespace(allowedConnection, quotaInBytes));
}

StorageManager::SessionStorageNamespace::SessionStorageNamespace(IPC::Connection* allowedConnection, unsigned quotaInBytes)
    : m_allowedConnection(allowedConnection)
    , m_quotaInBytes(quotaInBytes)
{
}

StorageManager::SessionStorageNamespace::~SessionStorageNamespace()
{
}

void StorageManager::SessionStorageNamespace::setAllowedConnection(IPC::Connection* allowedConnection)
{
    ASSERT(!allowedConnection || !m_allowedConnection);

    m_allowedConnection = allowedConnection;
}

PassRefPtr<StorageManager::StorageArea> StorageManager::SessionStorageNamespace::getOrCreateStorageArea(PassRefPtr<SecurityOrigin> securityOrigin)
{
    auto result = m_storageAreaMap.add(securityOrigin, nullptr);
    if (result.isNewEntry)
        result.iterator->value = StorageArea::create(0, result.iterator->key, m_quotaInBytes);

    return result.iterator->value;
}

void StorageManager::SessionStorageNamespace::cloneTo(SessionStorageNamespace& newSessionStorageNamespace)
{
    ASSERT_UNUSED(newSessionStorageNamespace, newSessionStorageNamespace.isEmpty());

    for (HashMap<RefPtr<SecurityOrigin>, RefPtr<StorageArea>>::const_iterator it = m_storageAreaMap.begin(), end = m_storageAreaMap.end(); it != end; ++it)
        newSessionStorageNamespace.m_storageAreaMap.add(it->key, it->value->clone());
}

PassRefPtr<StorageManager> StorageManager::create(const String& localStorageDirectory)
{
    return adoptRef(new StorageManager(localStorageDirectory));
}

StorageManager::StorageManager(const String& localStorageDirectory)
    : m_queue(WorkQueue::create("com.apple.WebKit.StorageManager"))
    , m_localStorageDatabaseTracker(LocalStorageDatabaseTracker::create(m_queue, localStorageDirectory))
{
    // Make sure the encoding is initialized before we start dispatching things to the queue.
    UTF8Encoding();
}

StorageManager::~StorageManager()
{
}

void StorageManager::createSessionStorageNamespace(uint64_t storageNamespaceID, IPC::Connection* allowedConnection, unsigned quotaInBytes)
{
    m_queue->dispatch(bind(&StorageManager::createSessionStorageNamespaceInternal, this, storageNamespaceID, RefPtr<IPC::Connection>(allowedConnection), quotaInBytes));
}

void StorageManager::destroySessionStorageNamespace(uint64_t storageNamespaceID)
{
    m_queue->dispatch(bind(&StorageManager::destroySessionStorageNamespaceInternal, this, storageNamespaceID));
}

void StorageManager::setAllowedSessionStorageNamespaceConnection(uint64_t storageNamespaceID, IPC::Connection* allowedConnection)
{
    m_queue->dispatch(bind(&StorageManager::setAllowedSessionStorageNamespaceConnectionInternal, this, storageNamespaceID, RefPtr<IPC::Connection>(allowedConnection)));
}

void StorageManager::cloneSessionStorageNamespace(uint64_t storageNamespaceID, uint64_t newStorageNamespaceID)
{
    m_queue->dispatch(bind(&StorageManager::cloneSessionStorageNamespaceInternal, this, storageNamespaceID, newStorageNamespaceID));
}

void StorageManager::processWillOpenConnection(WebProcessProxy* webProcessProxy)
{
    webProcessProxy->connection()->addWorkQueueMessageReceiver(Messages::StorageManager::messageReceiverName(), m_queue.get(), this);
}

void StorageManager::processWillCloseConnection(WebProcessProxy* webProcessProxy)
{
    webProcessProxy->connection()->removeWorkQueueMessageReceiver(Messages::StorageManager::messageReceiverName());

    m_queue->dispatch(bind(&StorageManager::invalidateConnectionInternal, this, RefPtr<IPC::Connection>(webProcessProxy->connection())));
}

void StorageManager::getOrigins(FunctionDispatcher& callbackDispatcher, void* context, void (*callback)(const Vector<RefPtr<WebCore::SecurityOrigin>>& securityOrigins, void* context))
{
    m_queue->dispatch(bind(&StorageManager::getOriginsInternal, this, RefPtr<FunctionDispatcher>(&callbackDispatcher), context, callback));
}

void StorageManager::getStorageDetailsByOrigin(FunctionDispatcher& callbackDispatcher, void* context, void (*callback)(const Vector<LocalStorageDetails>& storageDetails, void* context))
{
    m_queue->dispatch(bind(&StorageManager::getStorageDetailsByOriginInternal, this, RefPtr<FunctionDispatcher>(&callbackDispatcher), context, callback));
}

void StorageManager::deleteEntriesForOrigin(const SecurityOrigin& securityOrigin)
{
    m_queue->dispatch(bind(&StorageManager::deleteEntriesForOriginInternal, this, RefPtr<SecurityOrigin>(const_cast<SecurityOrigin*>(&securityOrigin))));
}

void StorageManager::deleteAllEntries()
{
    m_queue->dispatch(bind(&StorageManager::deleteAllEntriesInternal, this));
}

void StorageManager::createLocalStorageMap(IPC::Connection* connection, uint64_t storageMapID, uint64_t storageNamespaceID, const SecurityOriginData& securityOriginData)
{
    std::pair<RefPtr<IPC::Connection>, uint64_t> connectionAndStorageMapIDPair(connection, storageMapID);

    // FIXME: This should be a message check.
    ASSERT((HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::isValidKey(connectionAndStorageMapIDPair)));

    HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::AddResult result = m_storageAreasByConnection.add(connectionAndStorageMapIDPair, nullptr);

    // FIXME: These should be a message checks.
    ASSERT(result.isNewEntry);
    ASSERT((HashMap<uint64_t, RefPtr<LocalStorageNamespace>>::isValidKey(storageNamespaceID)));

    LocalStorageNamespace* localStorageNamespace = getOrCreateLocalStorageNamespace(storageNamespaceID);

    // FIXME: This should be a message check.
    ASSERT(localStorageNamespace);

    RefPtr<StorageArea> storageArea = localStorageNamespace->getOrCreateStorageArea(securityOriginData.securityOrigin());
    storageArea->addListener(connection, storageMapID);

    result.iterator->value = storageArea.release();
}

void StorageManager::createSessionStorageMap(IPC::Connection* connection, uint64_t storageMapID, uint64_t storageNamespaceID, const SecurityOriginData& securityOriginData)
{
    // FIXME: This should be a message check.
    ASSERT((HashMap<uint64_t, RefPtr<SessionStorageNamespace>>::isValidKey(storageNamespaceID)));
    SessionStorageNamespace* sessionStorageNamespace = m_sessionStorageNamespaces.get(storageNamespaceID);
    if (!sessionStorageNamespace) {
        // We're getting an incoming message from the web process that's for session storage for a web page
        // that has already been closed, just ignore it.
        return;
    }

    std::pair<RefPtr<IPC::Connection>, uint64_t> connectionAndStorageMapIDPair(connection, storageMapID);

    // FIXME: This should be a message check.
    ASSERT((HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::isValidKey(connectionAndStorageMapIDPair)));

    HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::AddResult result = m_storageAreasByConnection.add(connectionAndStorageMapIDPair, nullptr);

    // FIXME: This should be a message check.
    ASSERT(result.isNewEntry);

    // FIXME: This should be a message check.
    ASSERT(connection == sessionStorageNamespace->allowedConnection());

    RefPtr<StorageArea> storageArea = sessionStorageNamespace->getOrCreateStorageArea(securityOriginData.securityOrigin());
    storageArea->addListener(connection, storageMapID);

    result.iterator->value = storageArea.release();
}

void StorageManager::destroyStorageMap(IPC::Connection* connection, uint64_t storageMapID)
{
    std::pair<RefPtr<IPC::Connection>, uint64_t> connectionAndStorageMapIDPair(connection, storageMapID);

    // FIXME: This should be a message check.
    ASSERT((HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::isValidKey(connectionAndStorageMapIDPair)));

    HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::iterator it = m_storageAreasByConnection.find(connectionAndStorageMapIDPair);
    if (it == m_storageAreasByConnection.end()) {
        // The connection has been removed because the last page was closed.
        return;
    }

    it->value->removeListener(connection, storageMapID);
    m_storageAreasByConnection.remove(connectionAndStorageMapIDPair);
}

void StorageManager::getValues(IPC::Connection* connection, uint64_t storageMapID, uint64_t storageMapSeed, HashMap<String, String>& values)
{
    StorageArea* storageArea = findStorageArea(connection, storageMapID);
    if (!storageArea) {
        // This is a session storage area for a page that has already been closed. Ignore it.
        return;
    }

    values = storageArea->items();
    connection->send(Messages::StorageAreaMap::DidGetValues(storageMapSeed), storageMapID);
}

void StorageManager::setItem(IPC::Connection* connection, uint64_t storageMapID, uint64_t sourceStorageAreaID, uint64_t storageMapSeed, const String& key, const String& value, const String& urlString)
{
    StorageArea* storageArea = findStorageArea(connection, storageMapID);
    if (!storageArea) {
        // This is a session storage area for a page that has already been closed. Ignore it.
        return;
    }

    bool quotaError;
    storageArea->setItem(connection, sourceStorageAreaID, key, value, urlString, quotaError);
    connection->send(Messages::StorageAreaMap::DidSetItem(storageMapSeed, key, quotaError), storageMapID);
}

void StorageManager::removeItem(IPC::Connection* connection, uint64_t storageMapID, uint64_t sourceStorageAreaID, uint64_t storageMapSeed, const String& key, const String& urlString)
{
    StorageArea* storageArea = findStorageArea(connection, storageMapID);
    if (!storageArea) {
        // This is a session storage area for a page that has already been closed. Ignore it.
        return;
    }

    storageArea->removeItem(connection, sourceStorageAreaID, key, urlString);
    connection->send(Messages::StorageAreaMap::DidRemoveItem(storageMapSeed, key), storageMapID);
}

void StorageManager::clear(IPC::Connection* connection, uint64_t storageMapID, uint64_t sourceStorageAreaID, uint64_t storageMapSeed, const String& urlString)
{
    StorageArea* storageArea = findStorageArea(connection, storageMapID);
    if (!storageArea) {
        // This is a session storage area for a page that has already been closed. Ignore it.
        return;
    }

    storageArea->clear(connection, sourceStorageAreaID, urlString);
    connection->send(Messages::StorageAreaMap::DidClear(storageMapSeed), storageMapID);
}

void StorageManager::createSessionStorageNamespaceInternal(uint64_t storageNamespaceID, IPC::Connection* allowedConnection, unsigned quotaInBytes)
{
    ASSERT(!m_sessionStorageNamespaces.contains(storageNamespaceID));

    m_sessionStorageNamespaces.set(storageNamespaceID, SessionStorageNamespace::create(allowedConnection, quotaInBytes));
}

void StorageManager::destroySessionStorageNamespaceInternal(uint64_t storageNamespaceID)
{
    ASSERT(m_sessionStorageNamespaces.contains(storageNamespaceID));
    m_sessionStorageNamespaces.remove(storageNamespaceID);
}

void StorageManager::setAllowedSessionStorageNamespaceConnectionInternal(uint64_t storageNamespaceID, IPC::Connection* allowedConnection)
{
    ASSERT(m_sessionStorageNamespaces.contains(storageNamespaceID));

    m_sessionStorageNamespaces.get(storageNamespaceID)->setAllowedConnection(allowedConnection);
}

void StorageManager::cloneSessionStorageNamespaceInternal(uint64_t storageNamespaceID, uint64_t newStorageNamespaceID)
{
    SessionStorageNamespace* sessionStorageNamespace = m_sessionStorageNamespaces.get(storageNamespaceID);
    if (!sessionStorageNamespace) {
        // FIXME: We can get into this situation if someone closes the originating page from within a
        // createNewPage callback. We bail for now, but we should really find a way to keep the session storage alive
        // so we we'll clone the session storage correctly.
        return;
    }

    SessionStorageNamespace* newSessionStorageNamespace = m_sessionStorageNamespaces.get(newStorageNamespaceID);
    ASSERT(newSessionStorageNamespace);

    sessionStorageNamespace->cloneTo(*newSessionStorageNamespace);
}

void StorageManager::applicationWillTerminate()
{
    BinarySemaphore semaphore;
    m_queue->dispatch([this, &semaphore] {
        Vector<std::pair<RefPtr<IPC::Connection>, uint64_t>> connectionAndStorageMapIDPairsToRemove;
        for (auto& connectionStorageAreaPair : m_storageAreasByConnection) {
            connectionStorageAreaPair.value->removeListener(connectionStorageAreaPair.key.first.get(), connectionStorageAreaPair.key.second);
            connectionAndStorageMapIDPairsToRemove.append(connectionStorageAreaPair.key);
        }

        for (auto& connectionStorageAreaPair : connectionAndStorageMapIDPairsToRemove)
            m_storageAreasByConnection.remove(connectionStorageAreaPair);

        semaphore.signal();
    });
    semaphore.wait(std::numeric_limits<double>::max());
}

void StorageManager::invalidateConnectionInternal(IPC::Connection* connection)
{
    Vector<std::pair<RefPtr<IPC::Connection>, uint64_t>> connectionAndStorageMapIDPairsToRemove;
    HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>> storageAreasByConnection = m_storageAreasByConnection;
    for (HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::const_iterator it = storageAreasByConnection.begin(), end = storageAreasByConnection.end(); it != end; ++it) {
        if (it->key.first != connection)
            continue;

        it->value->removeListener(it->key.first.get(), it->key.second);
        connectionAndStorageMapIDPairsToRemove.append(it->key);
    }

    for (size_t i = 0; i < connectionAndStorageMapIDPairsToRemove.size(); ++i)
        m_storageAreasByConnection.remove(connectionAndStorageMapIDPairsToRemove[i]);
}

StorageManager::StorageArea* StorageManager::findStorageArea(IPC::Connection* connection, uint64_t storageMapID) const
{
    std::pair<IPC::Connection*, uint64_t> connectionAndStorageMapIDPair(connection, storageMapID);
    if (!HashMap<std::pair<RefPtr<IPC::Connection>, uint64_t>, RefPtr<StorageArea>>::isValidKey(connectionAndStorageMapIDPair))
        return 0;

    return m_storageAreasByConnection.get(connectionAndStorageMapIDPair);
}

StorageManager::LocalStorageNamespace* StorageManager::getOrCreateLocalStorageNamespace(uint64_t storageNamespaceID)
{
    if (!HashMap<uint64_t, RefPtr<LocalStorageNamespace>>::isValidKey(storageNamespaceID))
        return 0;

    HashMap<uint64_t, RefPtr<LocalStorageNamespace>>::AddResult result = m_localStorageNamespaces.add(storageNamespaceID, nullptr);
    if (result.isNewEntry)
        result.iterator->value = LocalStorageNamespace::create(this, storageNamespaceID);

    return result.iterator->value.get();
}

static void callCallbackFunction(void* context, void (*callbackFunction)(const Vector<RefPtr<WebCore::SecurityOrigin>>& securityOrigins, void* context), Vector<RefPtr<WebCore::SecurityOrigin>>* securityOriginsPtr)
{
    std::unique_ptr<Vector<RefPtr<WebCore::SecurityOrigin>>> securityOrigins(securityOriginsPtr);
    callbackFunction(*securityOrigins, context);
}

void StorageManager::getOriginsInternal(FunctionDispatcher* dispatcher, void* context, void (*callbackFunction)(const Vector<RefPtr<WebCore::SecurityOrigin>>& securityOrigins, void* context))
{
    auto securityOrigins = std::make_unique<Vector<RefPtr<WebCore::SecurityOrigin>>>(m_localStorageDatabaseTracker->origins());
    dispatcher->dispatch(bind(callCallbackFunction, context, callbackFunction, securityOrigins.release()));
}

void StorageManager::getStorageDetailsByOriginInternal(FunctionDispatcher* dispatcher, void* context, void (*callbackFunction)(const Vector<LocalStorageDetails>& storageDetails, void* context))
{
    Vector<LocalStorageDetails> storageDetails = m_localStorageDatabaseTracker->details();
    dispatcher->dispatch(bind(callbackFunction, WTF::move(storageDetails), context));
}

void StorageManager::deleteEntriesForOriginInternal(SecurityOrigin* securityOrigin)
{
    for (auto it = m_localStorageNamespaces.begin(), end = m_localStorageNamespaces.end(); it != end; ++it)
        it->value->clearStorageAreasMatchingOrigin(securityOrigin);

    m_localStorageDatabaseTracker->deleteDatabaseWithOrigin(securityOrigin);
}

void StorageManager::deleteAllEntriesInternal()
{
    for (auto it = m_localStorageNamespaces.begin(), end = m_localStorageNamespaces.end(); it != end; ++it)
        it->value->clearAllStorageAreas();

    m_localStorageDatabaseTracker->deleteAllDatabases();
}


} // namespace WebKit
