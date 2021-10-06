/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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

#ifndef ActionMenuHitTestResult_h
#define ActionMenuHitTestResult_h

#include "DataReference.h"
#include "DictionaryPopupInfo.h"
#include "ShareableBitmap.h"
#include "SharedMemory.h"
#include "WebHitTestResult.h"
#include <WebCore/FloatRect.h>
#include <WebCore/PageOverlay.h>
#include <WebCore/TextIndicator.h>
#include <wtf/text/WTFString.h>

OBJC_CLASS DDActionContext;

namespace IPC {
class ArgumentDecoder;
class ArgumentEncoder;
}

namespace WebKit {

struct ActionMenuHitTestResult {
    void encode(IPC::ArgumentEncoder&) const;
    static bool decode(IPC::ArgumentDecoder&, ActionMenuHitTestResult&);

    WebCore::FloatPoint hitTestLocationInViewCooordinates;
    WebHitTestResult::Data hitTestResult;

    String lookupText;
    RefPtr<SharedMemory> imageSharedMemory;
    String imageExtension;

    RetainPtr<DDActionContext> actionContext;
    WebCore::FloatRect detectedDataBoundingBox;
    RefPtr<WebCore::TextIndicator> detectedDataTextIndicator;
    WebCore::PageOverlay::PageOverlayID detectedDataOriginatingPageOverlay;

    DictionaryPopupInfo dictionaryPopupInfo;

    RefPtr<WebCore::TextIndicator> linkTextIndicator;
};

} // namespace WebKit

#endif // ActionMenuHitTestResult_h
