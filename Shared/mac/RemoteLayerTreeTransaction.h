/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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

#ifndef RemoteLayerTreeTransaction_h
#define RemoteLayerTreeTransaction_h

#include "PlatformCAAnimationRemote.h"
#include "RemoteLayerBackingStore.h"
#include <WebCore/Color.h>
#include <WebCore/FilterOperations.h>
#include <WebCore/FloatPoint3D.h>
#include <WebCore/FloatSize.h>
#include <WebCore/PlatformCALayer.h>
#include <WebCore/TransformationMatrix.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/text/WTFString.h>

namespace IPC {
class ArgumentDecoder;
class ArgumentEncoder;
}

namespace WebKit {

class PlatformCALayerRemote;

class RemoteLayerTreeTransaction {
public:
    enum LayerChanges {
        NoChange = 0,
        NameChanged = 1 << 1,
        ChildrenChanged = 1 << 2,
        PositionChanged = 1 << 3,
        BoundsChanged = 1 << 4,
        BackgroundColorChanged = 1 << 5,
        AnchorPointChanged = 1 << 6,
        BorderWidthChanged = 1 << 7,
        BorderColorChanged = 1 << 8,
        OpacityChanged = 1 << 9,
        TransformChanged = 1 << 10,
        SublayerTransformChanged = 1 << 11,
        HiddenChanged = 1 << 12,
        GeometryFlippedChanged = 1 << 13,
        DoubleSidedChanged = 1 << 14,
        MasksToBoundsChanged = 1 << 15,
        OpaqueChanged = 1 << 16,
        MaskLayerChanged = 1 << 17,
        ClonedContentsChanged = 1 << 18,
        ContentsRectChanged = 1 << 19,
        ContentsScaleChanged = 1 << 20,
        MinificationFilterChanged = 1 << 21,
        MagnificationFilterChanged = 1 << 22,
        BlendModeChanged = 1 << 23,
        SpeedChanged = 1 << 24,
        TimeOffsetChanged = 1 << 25,
        BackingStoreChanged = 1 << 26,
        FiltersChanged = 1 << 27,
        AnimationsChanged = 1 << 28,
        EdgeAntialiasingMaskChanged = 1 << 29,
        CustomAppearanceChanged = 1 << 30,
        CustomBehaviorChanged = 1 << 31
    };
    typedef unsigned LayerChange;

    struct LayerCreationProperties {
        LayerCreationProperties();

        void encode(IPC::ArgumentEncoder&) const;
        static bool decode(IPC::ArgumentDecoder&, LayerCreationProperties&);

        WebCore::GraphicsLayer::PlatformLayerID layerID;
        WebCore::PlatformCALayer::LayerType type;

        uint32_t hostingContextID;
        float hostingDeviceScaleFactor;
    };

    struct LayerProperties {
        LayerProperties();
        LayerProperties(const LayerProperties& other);

        void encode(IPC::ArgumentEncoder&) const;
        static bool decode(IPC::ArgumentDecoder&, LayerProperties&);

        void notePropertiesChanged(LayerChange changeFlags)
        {
            changedProperties |= changeFlags;
            everChangedProperties |= changeFlags;
        }

        void resetChangedProperties()
        {
            changedProperties = RemoteLayerTreeTransaction::NoChange;
        }

        LayerChange changedProperties;
        LayerChange everChangedProperties;

        String name;
        std::unique_ptr<WebCore::TransformationMatrix> transform;
        std::unique_ptr<WebCore::TransformationMatrix> sublayerTransform;
        Vector<WebCore::GraphicsLayer::PlatformLayerID> children;

        Vector<std::pair<String, PlatformCAAnimationRemote::Properties>> addedAnimations;
        HashSet<String> keyPathsOfAnimationsToRemove;

        WebCore::FloatPoint3D position;
        WebCore::FloatPoint3D anchorPoint;
        WebCore::FloatRect bounds;
        WebCore::FloatRect contentsRect;
        std::unique_ptr<RemoteLayerBackingStore> backingStore;
        std::unique_ptr<WebCore::FilterOperations> filters;
        WebCore::GraphicsLayer::PlatformLayerID maskLayerID;
        WebCore::GraphicsLayer::PlatformLayerID clonedLayerID;
        double timeOffset;
        float speed;
        float contentsScale;
        float borderWidth;
        float opacity;
        WebCore::Color backgroundColor;
        WebCore::Color borderColor;
        unsigned edgeAntialiasingMask;
        WebCore::GraphicsLayer::CustomAppearance customAppearance;
        WebCore::GraphicsLayer::CustomBehavior customBehavior;
        WebCore::PlatformCALayer::FilterType minificationFilter;
        WebCore::PlatformCALayer::FilterType magnificationFilter;
        WebCore::BlendMode blendMode;
        bool hidden;
        bool geometryFlipped;
        bool doubleSided;
        bool masksToBounds;
        bool opaque;
    };

    explicit RemoteLayerTreeTransaction();
    ~RemoteLayerTreeTransaction();

    void encode(IPC::ArgumentEncoder&) const;
    static bool decode(IPC::ArgumentDecoder&, RemoteLayerTreeTransaction&);

    WebCore::GraphicsLayer::PlatformLayerID rootLayerID() const { return m_rootLayerID; }
    void setRootLayerID(WebCore::GraphicsLayer::PlatformLayerID);
    void layerPropertiesChanged(PlatformCALayerRemote&);
    void setCreatedLayers(Vector<LayerCreationProperties>);
    void setDestroyedLayerIDs(Vector<WebCore::GraphicsLayer::PlatformLayerID>);
    void setLayerIDsWithNewlyUnreachableBackingStore(Vector<WebCore::GraphicsLayer::PlatformLayerID>);

#if !defined(NDEBUG) || !LOG_DISABLED
    WTF::CString description() const;
    void dump() const;
#endif

    typedef HashMap<WebCore::GraphicsLayer::PlatformLayerID, std::unique_ptr<LayerProperties>> LayerPropertiesMap;

    Vector<LayerCreationProperties> createdLayers() const { return m_createdLayers; }
    Vector<WebCore::GraphicsLayer::PlatformLayerID> destroyedLayers() const { return m_destroyedLayerIDs; }
    Vector<WebCore::GraphicsLayer::PlatformLayerID> layerIDsWithNewlyUnreachableBackingStore() const { return m_layerIDsWithNewlyUnreachableBackingStore; }

    Vector<RefPtr<PlatformCALayerRemote>>& changedLayers() { return m_changedLayers; }

    const LayerPropertiesMap& changedLayerProperties() const { return m_changedLayerProperties; }
    LayerPropertiesMap& changedLayerProperties() { return m_changedLayerProperties; }

    WebCore::IntSize contentsSize() const { return m_contentsSize; }
    void setContentsSize(const WebCore::IntSize& size) { m_contentsSize = size; };
    
    WebCore::Color pageExtendedBackgroundColor() const { return m_pageExtendedBackgroundColor; }
    void setPageExtendedBackgroundColor(WebCore::Color color) { m_pageExtendedBackgroundColor = color; }

    double pageScaleFactor() const { return m_pageScaleFactor; }
    void setPageScaleFactor(double pageScaleFactor) { m_pageScaleFactor = pageScaleFactor; }

    bool scaleWasSetByUIProcess() const { return m_scaleWasSetByUIProcess; }
    void setScaleWasSetByUIProcess(bool scaleWasSetByUIProcess) { m_scaleWasSetByUIProcess = scaleWasSetByUIProcess; }
    
    uint64_t renderTreeSize() const { return m_renderTreeSize; }
    void setRenderTreeSize(uint64_t renderTreeSize) { m_renderTreeSize = renderTreeSize; }

    double minimumScaleFactor() const { return m_minimumScaleFactor; }
    void setMinimumScaleFactor(double scale) { m_minimumScaleFactor = scale; }

    double maximumScaleFactor() const { return m_maximumScaleFactor; }
    void setMaximumScaleFactor(double scale) { m_maximumScaleFactor = scale; }

    bool allowsUserScaling() const { return m_allowsUserScaling; }
    void setAllowsUserScaling(bool allowsUserScaling) { m_allowsUserScaling = allowsUserScaling; }

    uint64_t transactionID() const { return m_transactionID; }
    void setTransactionID(uint64_t transactionID) { m_transactionID = transactionID; }

    typedef uint64_t TransactionCallbackID;
    const Vector<TransactionCallbackID>& callbackIDs() const { return m_callbackIDs; }
    void setCallbackIDs(Vector<TransactionCallbackID> callbackIDs) { m_callbackIDs = WTF::move(callbackIDs); }
    
private:
    WebCore::GraphicsLayer::PlatformLayerID m_rootLayerID;
    Vector<RefPtr<PlatformCALayerRemote>> m_changedLayers; // Only used in the Web process.
    LayerPropertiesMap m_changedLayerProperties; // Only used in the UI process.

    Vector<LayerCreationProperties> m_createdLayers;
    Vector<WebCore::GraphicsLayer::PlatformLayerID> m_destroyedLayerIDs;
    Vector<WebCore::GraphicsLayer::PlatformLayerID> m_videoLayerIDsPendingFullscreen;
    Vector<WebCore::GraphicsLayer::PlatformLayerID> m_layerIDsWithNewlyUnreachableBackingStore;

    Vector<TransactionCallbackID> m_callbackIDs;

    WebCore::IntSize m_contentsSize;
    WebCore::Color m_pageExtendedBackgroundColor;
    double m_pageScaleFactor;
    double m_minimumScaleFactor;
    double m_maximumScaleFactor;
    uint64_t m_renderTreeSize;
    uint64_t m_transactionID;
    bool m_scaleWasSetByUIProcess;
    bool m_allowsUserScaling;
};

} // namespace WebKit

#endif // RemoteLayerTreeTransaction_h
