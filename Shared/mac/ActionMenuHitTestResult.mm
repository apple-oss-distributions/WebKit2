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

#import "config.h"
#import "ActionMenuHitTestResult.h"

#if PLATFORM(MAC)

#import "ArgumentCodersCF.h"
#import "ArgumentDecoder.h"
#import "ArgumentEncoder.h"
#import "WebCoreArgumentCoders.h"
#import <WebCore/DataDetectorsSPI.h>
#import <WebCore/TextIndicator.h>

namespace WebKit {

void ActionMenuHitTestResult::encode(IPC::ArgumentEncoder& encoder) const
{
    encoder << hitTestLocationInViewCooordinates;
    encoder << hitTestResult;
    encoder << lookupText;
    encoder << imageExtension;

    SharedMemory::Handle imageHandle;
    if (imageSharedMemory && imageSharedMemory->size())
        imageSharedMemory->createHandle(imageHandle, SharedMemory::ReadOnly);
    encoder << imageHandle;

    bool hasActionContext = actionContext;
    encoder << hasActionContext;
    if (hasActionContext) {
        RetainPtr<NSMutableData> data = adoptNS([[NSMutableData alloc] init]);
        RetainPtr<NSKeyedArchiver> archiver = adoptNS([[NSKeyedArchiver alloc] initForWritingWithMutableData:data.get()]);
        [archiver setRequiresSecureCoding:YES];
        [archiver encodeObject:actionContext.get() forKey:@"actionContext"];
        [archiver finishEncoding];

        IPC::encode(encoder, reinterpret_cast<CFDataRef>(data.get()));

        encoder << detectedDataBoundingBox;
        encoder << detectedDataOriginatingPageOverlay;

        bool hasDetectedDataTextIndicator = detectedDataTextIndicator;
        encoder << hasDetectedDataTextIndicator;
        if (hasDetectedDataTextIndicator)
            encoder << detectedDataTextIndicator->data();
    }

    encoder << dictionaryPopupInfo;

    bool hasLinkTextIndicator = linkTextIndicator;
    encoder << hasLinkTextIndicator;
    if (hasLinkTextIndicator)
        encoder << linkTextIndicator->data();
}

bool ActionMenuHitTestResult::decode(IPC::ArgumentDecoder& decoder, ActionMenuHitTestResult& actionMenuHitTestResult)
{
    if (!decoder.decode(actionMenuHitTestResult.hitTestLocationInViewCooordinates))
        return false;

    if (!decoder.decode(actionMenuHitTestResult.hitTestResult))
        return false;

    if (!decoder.decode(actionMenuHitTestResult.lookupText))
        return false;

    if (!decoder.decode(actionMenuHitTestResult.imageExtension))
        return false;

    SharedMemory::Handle imageHandle;
    if (!decoder.decode(imageHandle))
        return false;

    if (!imageHandle.isNull())
        actionMenuHitTestResult.imageSharedMemory = SharedMemory::create(imageHandle, SharedMemory::ReadOnly);

    bool hasActionContext;
    if (!decoder.decode(hasActionContext))
        return false;

    if (hasActionContext) {
        RetainPtr<CFDataRef> data;
        if (!IPC::decode(decoder, data))
            return false;

        RetainPtr<NSKeyedUnarchiver> unarchiver = adoptNS([[NSKeyedUnarchiver alloc] initForReadingWithData:(NSData *)data.get()]);
        [unarchiver setRequiresSecureCoding:YES];
        @try {
            actionMenuHitTestResult.actionContext = [unarchiver decodeObjectOfClass:getDDActionContextClass() forKey:@"actionContext"];
        } @catch (NSException *exception) {
            LOG_ERROR("Failed to decode DDActionContext: %@", exception);
            return false;
        }
        
        [unarchiver finishDecoding];

        if (!decoder.decode(actionMenuHitTestResult.detectedDataBoundingBox))
            return false;

        if (!decoder.decode(actionMenuHitTestResult.detectedDataOriginatingPageOverlay))
            return false;

        bool hasDetectedDataTextIndicator;
        if (!decoder.decode(hasDetectedDataTextIndicator))
            return false;

        if (hasDetectedDataTextIndicator) {
            WebCore::TextIndicatorData indicatorData;
            if (!decoder.decode(indicatorData))
                return false;

            actionMenuHitTestResult.detectedDataTextIndicator = WebCore::TextIndicator::create(indicatorData);
        }
    }

    if (!decoder.decode(actionMenuHitTestResult.dictionaryPopupInfo))
        return false;

    bool hasLinkTextIndicator;
    if (!decoder.decode(hasLinkTextIndicator))
        return false;

    if (hasLinkTextIndicator) {
        WebCore::TextIndicatorData indicatorData;
        if (!decoder.decode(indicatorData))
            return false;

        actionMenuHitTestResult.linkTextIndicator = WebCore::TextIndicator::create(indicatorData);
    }

    return true;
}
    
} // namespace WebKit

#endif // PLATFORM(MAC)
