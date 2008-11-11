// Copyright (c) 2008 The Chromium Authors. All rights reserved.  Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include "config.h"
#include "ChromiumBridge.h"

#include "ClipboardUtilitiesChromium.h"
#include "Cursor.h"
#include "Frame.h"
#include "FrameView.h"
#include "HostWindow.h"
#include "KURL.h"
#include "NativeImageSkia.h"
#include "Page.h"
#include "PasteboardPrivate.h"
#include "PlatformString.h"
#include "PlatformWidget.h"
#include "ScrollView.h"
#include "Widget.h"

#undef LOG
#include "base/clipboard.h"
#include "base/string_util.h"
#include "webkit/glue/chrome_client_impl.h"
#include "webkit/glue/glue_util.h"
#include "webkit/glue/scoped_clipboard_writer_glue.h"
#include "webkit/glue/webcursor.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/glue/webview_impl.h"
#include "webkit/glue/webview_delegate.h"

namespace {

PlatformWidget ToPlatform(WebCore::Widget* widget) {
  return widget ? widget->root()->hostWindow()->platformWindow() : 0;
}

ChromeClientImpl* ToChromeClient(WebCore::Widget* widget) {
  WebCore::FrameView* view;
  if (widget->isFrameView()) {
    view = static_cast<WebCore::FrameView*>(widget);
  } else if (widget->parent() && widget->parent()->isFrameView()) {
    view = static_cast<WebCore::FrameView*>(widget->parent());
  } else {
    return NULL;
  }

  WebCore::Page* page = view->frame() ? view->frame()->page() : NULL;
  if (!page)
    return NULL;

  return static_cast<ChromeClientImpl*>(page->chrome()->client());
}

std::wstring UrlToImageMarkup(const WebCore::KURL& url,
                              const WebCore::String& alt_str) {
  std::wstring markup(L"<img src=\"");
  markup.append(webkit_glue::StringToStdWString(url.string()));
  markup.append(L"\"");
  if (!alt_str.isEmpty()) {
    markup.append(L" alt=\"");
    std::wstring alt_stdstr = webkit_glue::StringToStdWString(alt_str);
    ReplaceSubstringsAfterOffset(&alt_stdstr, 0, L"\"", L"&quot;");
    markup.append(alt_stdstr);
    markup.append(L"\"");
  }
  markup.append(L"/>");
  return markup;
}

}  // namespace

namespace WebCore {

bool ChromiumBridge::clipboardIsFormatAvailable(
    PasteboardPrivate::ClipboardFormat format) {
  switch (format) {
    case PasteboardPrivate::HTMLFormat:
      return webkit_glue::ClipboardIsFormatAvailable(
          ::Clipboard::GetHtmlFormatType());

    case PasteboardPrivate::BookmarkFormat:
#if defined(OS_WIN) || defined(OS_MACOSX)
      return webkit_glue::ClipboardIsFormatAvailable(
          ::Clipboard::GetUrlWFormatType());
#endif

#if defined(OS_WIN)
    // TODO(tc): This should work for linux/mac too.
    case PasteboardPrivate::WebSmartPasteFormat:
      return webkit_glue::ClipboardIsFormatAvailable(
          ::Clipboard::GetWebKitSmartPasteFormatType());
#endif

    default:
      NOTREACHED();
      return false;
  }
}

String ChromiumBridge::clipboardReadPlainText() {
  if (webkit_glue::ClipboardIsFormatAvailable(
      ::Clipboard::GetPlainTextWFormatType())) {
    std::wstring text;
    webkit_glue::ClipboardReadText(&text);
    if (!text.empty())
      return webkit_glue::StdWStringToString(text);
  }

  if (webkit_glue::ClipboardIsFormatAvailable(
      ::Clipboard::GetPlainTextFormatType())) {
    std::string text;
    webkit_glue::ClipboardReadAsciiText(&text);
    if (!text.empty())
      return webkit_glue::StdStringToString(text);
  }

  return String();
}

void ChromiumBridge::clipboardReadHTML(String* html, KURL* url) {
  std::wstring html_stdstr;
  GURL gurl;
  webkit_glue::ClipboardReadHTML(&html_stdstr, &gurl);
  *html = webkit_glue::StdWStringToString(html_stdstr);
  *url = webkit_glue::GURLToKURL(gurl);
}

void ChromiumBridge::clipboardWriteSelection(const String& html,
                                             const KURL& url,
                                             const String& plain_text,
                                             bool can_smart_copy_or_delete) {
  ScopedClipboardWriterGlue scw(webkit_glue::ClipboardGetClipboard());
  scw.WriteHTML(webkit_glue::StringToStdWString(html),
                webkit_glue::CStringToStdString(url.utf8String()));
  scw.WriteText(webkit_glue::StringToStdWString(plain_text));

#if defined(OS_WIN)
  if (can_smart_copy_or_delete)
    scw.WriteWebSmartPaste();
#endif
}

void ChromiumBridge::clipboardWriteURL(const KURL& url, const String& title) {
  ScopedClipboardWriterGlue scw(webkit_glue::ClipboardGetClipboard());

  GURL gurl = webkit_glue::KURLToGURL(url);
  scw.WriteBookmark(webkit_glue::StringToStdWString(title), gurl.spec());

  std::wstring link(webkit_glue::StringToStdWString(urlToMarkup(url, title)));
  scw.WriteHTML(link, "");

  scw.WriteText(ASCIIToWide(gurl.spec()));
}

void ChromiumBridge::clipboardWriteImage(const NativeImageSkia* bitmap,
    const KURL& url, const String& title) {
  ScopedClipboardWriterGlue scw(webkit_glue::ClipboardGetClipboard());

#if defined(OS_WIN)
  if (bitmap)
    scw.WriteBitmap(*bitmap);
#endif
  if (!url.isEmpty()) {
    GURL gurl = webkit_glue::KURLToGURL(url);
    scw.WriteBookmark(webkit_glue::StringToStdWString(title), gurl.spec());

    scw.WriteHTML(UrlToImageMarkup(url, title), "");

    scw.WriteText(ASCIIToWide(gurl.spec()));
  }
}

// 

// Cookies --------------------------------------------------------------------

void ChromiumBridge::setCookies(
    const KURL& url, const KURL& policy_url, const String& cookie) {
  webkit_glue::SetCookie(
      webkit_glue::KURLToGURL(url),
      webkit_glue::KURLToGURL(policy_url),
      webkit_glue::StringToStdString(cookie));
}

String ChromiumBridge::cookies(const KURL& url, const KURL& policy_url) {
  return webkit_glue::StdStringToString(webkit_glue::GetCookies(
      webkit_glue::KURLToGURL(url),
      webkit_glue::KURLToGURL(policy_url)));
}

// DNS ------------------------------------------------------------------------

void ChromiumBridge::prefetchDNS(const String& hostname) {
  webkit_glue::PrefetchDns(webkit_glue::StringToStdString(hostname));
}

// Language -------------------------------------------------------------------

String ChromiumBridge::computedDefaultLanguage() {
  return webkit_glue::StdWStringToString(webkit_glue::GetWebKitLocale());
}

// Screen ---------------------------------------------------------------------

int ChromiumBridge::screenDepth(Widget* widget) {
  return webkit_glue::GetScreenInfo(ToPlatform(widget)).depth;
}

int ChromiumBridge::screenDepthPerComponent(Widget* widget) {
  return webkit_glue::GetScreenInfo(ToPlatform(widget)).depth_per_component;
}

bool ChromiumBridge::screenIsMonochrome(Widget* widget) {
  return webkit_glue::GetScreenInfo(ToPlatform(widget)).is_monochrome;
}

IntRect ChromiumBridge::screenRect(Widget* widget) {
  return webkit_glue::ToIntRect(
      webkit_glue::GetScreenInfo(ToPlatform(widget)).rect);
}

IntRect ChromiumBridge::screenAvailableRect(Widget* widget) {
  return webkit_glue::ToIntRect(
      webkit_glue::GetScreenInfo(ToPlatform(widget)).available_rect);
}

// Widget ---------------------------------------------------------------------

void ChromiumBridge::widgetSetCursor(Widget* widget, const Cursor& cursor) {
  ChromeClientImpl* chrome_client = ToChromeClient(widget);
  if (chrome_client)
    chrome_client->SetCursor(WebCursor(cursor.impl()));
}

void ChromiumBridge::widgetSetFocus(Widget* widget) {
  ChromeClientImpl* chrome_client = ToChromeClient(widget);
  if (chrome_client)
    chrome_client->focus();
}

}  // namespace WebCore
