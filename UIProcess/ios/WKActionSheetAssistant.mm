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
#import "WKActionSheetAssistant.h"

#if PLATFORM(IOS)

#import "APIUIClient.h"
#import "WKActionSheet.h"
#import "WKContentViewInteraction.h"
#import "WebPageProxy.h"
#import "_WKActivatedElementInfoInternal.h"
#import "_WKElementActionInternal.h"
#import <DataDetectorsUI/DDAction.h>
#import <DataDetectorsUI/DDDetectionController.h>
#import <SafariServices/SSReadingList.h>
#import <TCC/TCC.h>
#import <UIKit/UIAlertController_Private.h>
#import <UIKit/UIView.h>
#import <UIKit/UIViewController_Private.h>
#import <UIKit/UIWindow_Private.h>
#import <WebCore/LocalizedStrings.h>
#import <WebCore/SoftLinking.h>
#import <WebCore/WebCoreNSURLExtras.h>
#import <wtf/text/WTFString.h>

SOFT_LINK_FRAMEWORK(SafariServices)
SOFT_LINK_CLASS(SafariServices, SSReadingList)

SOFT_LINK_PRIVATE_FRAMEWORK(TCC)
SOFT_LINK(TCC, TCCAccessPreflight, TCCAccessPreflightResult, (CFStringRef service, CFDictionaryRef options), (service, options))
SOFT_LINK_CONSTANT(TCC, kTCCServicePhotos, CFStringRef)

SOFT_LINK_PRIVATE_FRAMEWORK(DataDetectorsUI)
SOFT_LINK_CLASS(DataDetectorsUI, DDDetectionController)

// FIXME: This will be removed as soon as <rdar://problem/16346913> is fixed.
@interface DDDetectionController (WKDDActionPrivate)
- (NSArray *)actionsForAnchor:(id)anchor url:(NSURL *)targetURL forFrame:(id)frame;
@end

using namespace WebKit;

@implementation WKActionSheetAssistant {
    RetainPtr<WKActionSheet> _interactionSheet;
    RetainPtr<_WKActivatedElementInfo> _elementInfo;
    WKContentView *_view;
}

- (id)initWithView:(WKContentView *)view
{
    _view = view;
    return self;
}

- (void)dealloc
{
    [self cleanupSheet];
    [super dealloc];
}

- (UIView *)superviewForSheet
{
    UIView *view = [_view window];

    // FIXME: WebKit has a delegate to retrieve the superview for the image sheet (superviewForImageSheetForWebView)
    // Do we need it in WK2?

    // Find the top most view with a view controller
    UIViewController *controller = nil;
    UIView *currentView = _view;
    while (currentView) {
        UIViewController *aController = [UIViewController viewControllerForView:currentView];
        if (aController)
            controller = aController;

        currentView = [currentView superview];
    }
    if (controller)
        view = controller.view;

    return view;
}

- (CGRect)_presentationRectForSheetGivenPoint:(CGPoint)point inHostView:(UIView *)hostView
{
    CGPoint presentationPoint = [hostView convertPoint:point fromView:_view];
    CGRect presentationRect = CGRectMake(presentationPoint.x, presentationPoint.y, 1.0, 1.0);

    return CGRectInset(presentationRect, -22.0, -22.0);
}

- (UIView *)hostViewForSheet
{
    return [self superviewForSheet];
}

- (CGRect)initialPresentationRectInHostViewForSheet
{
    UIView *view = [self superviewForSheet];
    if (!view)
        return CGRectZero;

    return [self _presentationRectForSheetGivenPoint:_view.positionInformation.point inHostView:view];

}

- (CGRect)presentationRectInHostViewForSheet
{
    UIView *view = [self superviewForSheet];
    if (!view)
        return CGRectZero;

    CGRect boundingRect = _view.positionInformation.bounds;
    CGPoint fromPoint = _view.positionInformation.point;

    // FIXME: We must adjust our presentation point to take into account a change in document scale.

    // Test to see if we are still within the target node as it may have moved after rotation.
    if (!CGRectContainsPoint(boundingRect, fromPoint))
        fromPoint = CGPointMake(CGRectGetMidX(boundingRect), CGRectGetMidY(boundingRect));

    return [self _presentationRectForSheetGivenPoint:fromPoint inHostView:view];
}

- (BOOL)presentSheet
{
    // Calculate the presentation rect just before showing.
    CGRect presentationRect = CGRectZero;
    if (UI_USER_INTERFACE_IDIOM() != UIUserInterfaceIdiomPhone) {
        presentationRect = [self initialPresentationRectInHostViewForSheet];
        if (CGRectIsEmpty(presentationRect))
            return NO;
    }

    return [_interactionSheet presentSheetFromRect:presentationRect];
}

- (void)updateSheetPosition
{
    [_interactionSheet updateSheetPosition];
}

- (void)_createSheetWithElementActions:(NSArray *)actions showLinkTitle:(BOOL)showLinkTitle
{
    ASSERT(!_interactionSheet);

    NSURL *targetURL = [NSURL URLWithString:_view.positionInformation.url];
    NSString *urlScheme = [targetURL scheme];
    BOOL isJavaScriptURL = [urlScheme length] && [urlScheme caseInsensitiveCompare:@"javascript"] == NSOrderedSame;
    // FIXME: We should check if Javascript is enabled in the preferences.

    _interactionSheet = adoptNS([[WKActionSheet alloc] initWithView:_view]);
    _interactionSheet.get().sheetDelegate = self;
    _interactionSheet.get().preferredStyle = UIAlertControllerStyleActionSheet;

    NSString *titleString = nil;
    BOOL titleIsURL = NO;
    if (showLinkTitle && [[targetURL absoluteString] length]) {
        if (isJavaScriptURL)
            titleString = WEB_UI_STRING_KEY("JavaScript", "JavaScript Action Sheet Title", "Title for action sheet for JavaScript link");
        else {
            titleString = WebCore::userVisibleString(targetURL);
            titleIsURL = YES;
        }
    } else
        titleString = _view.positionInformation.title;

    if ([titleString length]) {
        [_interactionSheet setTitle:titleString];
        // We should configure the text field's line breaking mode correctly here, based on whether
        // the title is an URL or not, but the appropriate UIAlertController SPIs are not available yet.
        // The code that used to do this in the UIActionSheet world has been saved for reference in
        // <rdar://problem/17049781> Configure the UIAlertController's title appropriately.
    }

    for (_WKElementAction *action in actions) {
        [_interactionSheet _addActionWithTitle:[action title] style:UIAlertActionStyleDefault handler:^{
            [action _runActionWithElementInfo:_elementInfo.get() view:_view];
            [self cleanupSheet];
        } shouldDismissHandler:^{
            return (BOOL)(!action.dismissalHandler || action.dismissalHandler());
        }];
    }

    [_interactionSheet addAction:[UIAlertAction actionWithTitle:WEB_UI_STRING_KEY("Cancel", "Cancel button label in button bar", "Title for Cancel button label in button bar")
                                                          style:UIAlertActionStyleCancel
                                                        handler:^(UIAlertAction *action) {
                                                            [self cleanupSheet];
                                                        }]];
    _view.page->startInteractionWithElementAtPosition(_view.positionInformation.point);
}

- (void)showImageSheet
{
    ASSERT(!_interactionSheet);
    ASSERT(!_elementInfo);

    const auto& positionInformation = _view.positionInformation;

    NSURL *targetURL = [NSURL URLWithString:positionInformation.url];
    auto defaultActions = adoptNS([[NSMutableArray alloc] init]);
    if (!positionInformation.url.isEmpty())
        [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeOpen]];
    if ([getSSReadingListClass() supportsURL:targetURL])
        [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeAddToReadingList]];
    if (TCCAccessPreflight(getkTCCServicePhotos(), NULL) != kTCCAccessPreflightDenied)
        [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeSaveImage]];
    if (!targetURL.scheme.length || [targetURL.scheme caseInsensitiveCompare:@"javascript"] != NSOrderedSame)
        [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeCopy]];

    auto elementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeImage
        URL:targetURL location:positionInformation.point title:positionInformation.title rect:positionInformation.bounds image:positionInformation.image.get()]);

    RetainPtr<NSArray> actions = _view.page->uiClient().actionsForElement(elementInfo.get(), WTF::move(defaultActions));

    if (![actions count])
        return;

    [self _createSheetWithElementActions:actions.get() showLinkTitle:YES];
    if (!_interactionSheet)
        return;

    _elementInfo = WTF::move(elementInfo);

    if (![_interactionSheet presentSheet])
        [self cleanupSheet];
}

- (void)showLinkSheet
{
    ASSERT(!_interactionSheet);
    ASSERT(!_elementInfo);

    const auto& positionInformation = _view.positionInformation;

    NSURL *targetURL = [NSURL URLWithString:positionInformation.url];
    if (!targetURL)
        return;

    auto defaultActions = adoptNS([[NSMutableArray alloc] init]);
    [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeOpen]];
    if ([getSSReadingListClass() supportsURL:targetURL])
        [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeAddToReadingList]];
    if (![[targetURL scheme] length] || [[targetURL scheme] caseInsensitiveCompare:@"javascript"] != NSOrderedSame)
        [defaultActions addObject:[_WKElementAction elementActionWithType:_WKElementActionTypeCopy]];

    RetainPtr<_WKActivatedElementInfo> elementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeLink
        URL:targetURL location:positionInformation.point title:positionInformation.title rect:positionInformation.bounds image:positionInformation.image.get()]);

    RetainPtr<NSArray> actions = _view.page->uiClient().actionsForElement(elementInfo.get(), WTF::move(defaultActions));

    if (![actions count])
        return;

    [self _createSheetWithElementActions:actions.get() showLinkTitle:YES];
    if (!_interactionSheet)
        return;

    _elementInfo = WTF::move(elementInfo);

    if (![_interactionSheet presentSheet])
        [self cleanupSheet];
}

- (void)showDataDetectorsSheet
{
    ASSERT(!_interactionSheet);
    NSURL *targetURL = [NSURL URLWithString:_view.positionInformation.url];
    if (!targetURL)
        return;

    if (![[getDDDetectionControllerClass() tapAndHoldSchemes] containsObject:[targetURL scheme]])
        return;

    NSArray *dataDetectorsActions = [[getDDDetectionControllerClass() sharedController] actionsForAnchor:nil url:targetURL forFrame:nil];
    if ([dataDetectorsActions count] == 0)
        return;

    NSMutableArray *elementActions = [NSMutableArray array];
    for (NSUInteger actionNumber = 0; actionNumber < [dataDetectorsActions count]; actionNumber++) {
        DDAction *action = [dataDetectorsActions objectAtIndex:actionNumber];
        _WKElementAction *elementAction = [_WKElementAction elementActionWithTitle:[action localizedName] actionHandler:^(_WKActivatedElementInfo *actionInfo) {
            [[getDDDetectionControllerClass() sharedController] performAction:action
                                                          fromAlertController:_interactionSheet.get()
                                                          interactionDelegate:self];
        }];
        elementAction.dismissalHandler = ^{
            return (BOOL)!action.hasUserInterface;
        };
        [elementActions addObject:elementAction];
    }

    [self _createSheetWithElementActions:elementActions showLinkTitle:NO];
    if (!_interactionSheet)
        return;

    if (elementActions.count <= 1)
        _interactionSheet.get().arrowDirections = UIPopoverArrowDirectionUp | UIPopoverArrowDirectionDown;

    if (![_interactionSheet presentSheet])
        [self cleanupSheet];
}

- (void)cleanupSheet
{
    _view.page->stopInteraction();

    [_interactionSheet doneWithSheet];
    [_interactionSheet setSheetDelegate:nil];
    _interactionSheet = nil;
    _elementInfo = nil;
}

@end

#endif // PLATFORM(IOS)
