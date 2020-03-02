/*
 * Copyright (C) 2019 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "WebKitInputMethodContext.h"

#include "WebKitInputMethodContextPrivate.h"
#include "WebKitWebView.h"
#include <wtf/glib/WTFGType.h>

using namespace WebCore;

/**
 * SECTION: WebKitInputMethodContext
 * @Short_description: Base class for input method contexts
 * @Title: WebKitInputMethodContext
 * @See_also: #WebKitWebView
 *
 * WebKitInputMethodContext defines the interface to implement WebKit input methods.
 * The input methods are used by WebKit, when editable content is focused, to map from
 * key events to Unicode character strings.
 *
 * An input method may consume multiple key events in sequence and finally
 * output the composed result. This is called preediting, and an input method
 * may provide feedback about this process by displaying the intermediate
 * composition states as preedit text.
 *
 * Since: 2.28
 */

enum {
    PREEDIT_STARTED,
    PREEDIT_CHANGED,
    PREEDIT_FINISHED,

    COMMITTED,

    LAST_SIGNAL
};

G_DEFINE_BOXED_TYPE(WebKitInputMethodUnderline, webkit_input_method_underline, webkit_input_method_underline_copy, webkit_input_method_underline_free)

const CompositionUnderline& webkitInputMethodUnderlineGetCompositionUnderline(WebKitInputMethodUnderline* underline)
{
    return underline->underline;
}

/**
 * webkit_input_method_underline_new:
 * @start_offset: the start offset in preedit string
 * @end_offset: the end offset in preedit string
 *
 * Create a new #WebKitInputMethodUnderline for the given range in preedit string
 *
 * Returns: (transfer full): A newly created #WebKitInputMethodUnderline
 *
 * Since: 2.28
 */
WebKitInputMethodUnderline* webkit_input_method_underline_new(unsigned startOffset, unsigned endOffset)
{
    auto* underline = static_cast<WebKitInputMethodUnderline*>(fastMalloc(sizeof(WebKitInputMethodUnderline)));
    new (underline) WebKitInputMethodUnderline(startOffset, endOffset);
    return underline;
}

/**
 * webkit_input_method_underline_copy:
 * @underline: a #WebKitInputMethodUnderline
 *
 * Make a copy of the #WebKitInputMethodUnderline.
 *
 * Returns: (transfer full): A copy of passed in #WebKitInputMethodUnderline
 *
 * Since: 2.28
 */
WebKitInputMethodUnderline* webkit_input_method_underline_copy(WebKitInputMethodUnderline* underline)
{
    g_return_val_if_fail(underline, nullptr);

    auto* copyUnderline = static_cast<WebKitInputMethodUnderline*>(fastMalloc(sizeof(WebKitInputMethodUnderline)));
    new (copyUnderline) WebKitInputMethodUnderline(underline->underline);
    return copyUnderline;
}

/**
 * webkit_input_method_underline_free:
 * @underline: A #WebKitInputMethodUnderline
 *
 * Free the #WebKitInputMethodUnderline.
 *
 * Since: 2.28
 */
void webkit_input_method_underline_free(WebKitInputMethodUnderline* underline)
{
    g_return_if_fail(underline);

    underline->~WebKitInputMethodUnderline();
    fastFree(underline);
}

struct _WebKitInputMethodContextPrivate {
    WebKitWebView* webView;
};

static guint signals[LAST_SIGNAL] = { 0, };

WEBKIT_DEFINE_ABSTRACT_TYPE(WebKitInputMethodContext, webkit_input_method_context, G_TYPE_OBJECT)

/**
 * WebKitInputMethodContextClass:
 * @set_enable_preedit: Called via webkit_input_method_context_set_enable_preedit() to
 *   control the use of the preedit string.
 * @get_preedit: Called via webkit_input_method_context_get_preedit() to
 *   retrieve the text currently being preedited for display at the cursor
 *   position. Any input method which composes complex characters or any
 *   other compositions from multiple sequential key presses should override
 *   this method to provide feedback.
 * @filter_key_event: Called via webkit_input_method_context_filter_key_event() on every
 *   key press or release event. Every non-trivial input method needs to
 *   override this in order to implement the mapping from key events to text.
 *   A return value of %TRUE indicates to the caller that the event was
 *   consumed by the input method. In that case, the #WebKitInputMethodContext::committed
 *   signal should be emitted upon completion of a key sequence to pass the
 *   resulting text back to the editable element. Alternatively, %FALSE may be
 *   returned to indicate that the event wasn’t handled by the input method.
 * @notify_focus_in: Called via webkit_input_method_context_notify_focus_in() when
 *   an editable element of the #WebKitWebView has gained focus.
 * @notify_focus_out: Called via webkit_input_method_context_notify_focus_out() when
 *   an editable element of the #WebKitWebView has lost focus.
 * @notify_cursor_area: Called via webkit_input_method_context_notify_cursor_area()
 *   to inform the input method of the current cursor location relative to
 *   the client window.
 * @reset: Called via webkit_input_method_context_reset() to signal a change that
 *   requires a reset. An input method that implements preediting
 *   should override this method to clear the preedit state on reset.
 *
 * Since: 2.28
 */

static void webkit_input_method_context_class_init(WebKitInputMethodContextClass* klass)
{
    /**
     * WebKitInputMethodContext::preedit-started
     * @context: the #WebKitInputMethodContext on which the signal is emitted
     *
     * Emitted when a new preediting sequence starts.
     *
     * Since: 2.28
     */
    signals[PREEDIT_STARTED] = g_signal_new(
        "preedit-started",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitInputMethodContextClass, preedit_started),
        nullptr, nullptr,
        g_cclosure_marshal_generic,
        G_TYPE_NONE, 0);

    /**
     * WebKitInputMethodContext::preedit-changed
     * @context: the #WebKitInputMethodContext on which the signal is emitted
     *
     * Emitted whenever the preedit sequence currently being entered has changed.
     * It is also emitted at the end of a preedit sequence, in which case
     * webkit_input_method_context_get_preedit() returns the empty string.
     *
     * Since: 2.28
     */
    signals[PREEDIT_CHANGED] = g_signal_new(
        "preedit-changed",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitInputMethodContextClass, preedit_changed),
        nullptr, nullptr,
        g_cclosure_marshal_generic,
        G_TYPE_NONE, 0);

    /**
     * WebKitInputMethodContext::preedit-finished
     * @context: the #WebKitInputMethodContext on which the signal is emitted
     *
     * Emitted when a preediting sequence has been completed or canceled.
     *
     * Since: 2.28
     */
    signals[PREEDIT_FINISHED] = g_signal_new(
        "preedit-finished",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitInputMethodContextClass, preedit_finished),
        nullptr, nullptr,
        g_cclosure_marshal_generic,
        G_TYPE_NONE, 0);

    /**
     * WebKitInputMethodContext::committed
     * @context: the #WebKitInputMethodContext on which the signal is emitted
     * @text: the string result
     *
     * Emitted when a complete input sequence has been entered by the user.
     * This can be a single character immediately after a key press or the
     * final result of preediting.
     *
     * Since: 2.28
     */
    signals[COMMITTED] = g_signal_new(
        "committed",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitInputMethodContextClass, committed),
        nullptr, nullptr,
        g_cclosure_marshal_generic,
        G_TYPE_NONE, 1,
        G_TYPE_STRING);
}

void webkitInputMethodContextSetWebView(WebKitInputMethodContext* context, WebKitWebView* webView)
{
    context->priv->webView = webView;
}

WebKitWebView* webkitInputMethodContextGetWebView(WebKitInputMethodContext* context)
{
    return context->priv->webView;
}

/**
 * webkit_input_method_context_set_enable_preedit:
 * @context: a #WebKitInputMethodContext
 * @enabled: whether to enable preedit
 *
 * Set whether @context should enable preedit to display feedback.
 *
 * Since: 2.28
 */
void webkit_input_method_context_set_enable_preedit(WebKitInputMethodContext* context, gboolean enabled)
{
    g_return_if_fail(WEBKIT_IS_INPUT_METHOD_CONTEXT(context));

    auto* imClass = WEBKIT_INPUT_METHOD_CONTEXT_GET_CLASS(context);
    if (imClass->set_enable_preedit)
        imClass->set_enable_preedit(context, enabled);
}

/**
 * webkit_input_method_context_get_preedit:
 * @context: a #WebKitInputMethodContext
 * @text: (out) (transfer full) (nullable): location to store the preedit string
 * @underlines: (out) (transfer full) (nullable) (element-type WebKit2.InputMethodUnderline): location to store the underlines as a #GList of #WebKitInputMethodUnderline
 * @cursor_offset: (out) (nullable): location to store the position of cursor in preedit string
 *
 * Get the current preedit string for the @context, and a list of WebKitInputMethodUnderline to apply to the string.
 * The string will be displayed inserted at @cursor_offset.
 *
 * Since: 2.28
 */
void webkit_input_method_context_get_preedit(WebKitInputMethodContext* context, char** text, GList** underlines, unsigned* cursorOffset)
{
    g_return_if_fail(WEBKIT_IS_INPUT_METHOD_CONTEXT(context));

    auto* imClass = WEBKIT_INPUT_METHOD_CONTEXT_GET_CLASS(context);
    if (imClass->get_preedit) {
        imClass->get_preedit(context, text, underlines, cursorOffset);
        return;
    }

    if (text)
        *text = g_strdup("");
    if (underlines)
        *underlines = nullptr;
    if (cursorOffset)
        *cursorOffset = 0;
}

/**
 * webkit_input_method_context_notify_focus_in:
 * @context: a #WebKitInputMethodContext
 *
 * Notify @context that input associated has gained focus.
 *
 * Since: 2.28
 */
void webkit_input_method_context_notify_focus_in(WebKitInputMethodContext* context)
{
    g_return_if_fail(WEBKIT_IS_INPUT_METHOD_CONTEXT(context));

    auto* imClass = WEBKIT_INPUT_METHOD_CONTEXT_GET_CLASS(context);
    if (imClass->notify_focus_in)
        imClass->notify_focus_in(context);
}

/**
 * webkit_input_method_context_notify_focus_out:
 * @context: a #WebKitInputMethodContext
 *
 * Notify @context that input associated has lost focus.
 *
 * Since: 2.28
 */
void webkit_input_method_context_notify_focus_out(WebKitInputMethodContext* context)
{
    g_return_if_fail(WEBKIT_IS_INPUT_METHOD_CONTEXT(context));

    auto* imClass = WEBKIT_INPUT_METHOD_CONTEXT_GET_CLASS(context);
    if (imClass->notify_focus_out)
        imClass->notify_focus_out(context);
}

/**
 * webkit_input_method_context_notify_cursor_area:
 * @context: a #WebKitInputMethodContext
 * @x: the x coordinate of cursor location
 * @y: the y coordinate of cursor location
 * @width: the width of cursor area
 * @height: the height of cursor area
 *
 * Notify @context that cursor area changed in input associated.
 *
 * Since: 2.28
 */
void webkit_input_method_context_notify_cursor_area(WebKitInputMethodContext* context, int x, int y, int width, int height)
{
    g_return_if_fail(WEBKIT_IS_INPUT_METHOD_CONTEXT(context));

    auto* imClass = WEBKIT_INPUT_METHOD_CONTEXT_GET_CLASS(context);
    if (imClass->notify_cursor_area)
        imClass->notify_cursor_area(context, x, y, width, height);
}

/**
 * webkit_input_method_context_reset:
 * @context: a #WebKitInputMethodContext
 *
 * Reset the @context. This will typically cause the input to clear the preedit state.
 *
 * Since: 2.28
 */
void webkit_input_method_context_reset(WebKitInputMethodContext* context)
{
    g_return_if_fail(WEBKIT_IS_INPUT_METHOD_CONTEXT(context));

    auto* imClass = WEBKIT_INPUT_METHOD_CONTEXT_GET_CLASS(context);
    if (imClass->reset)
        imClass->reset(context);
}