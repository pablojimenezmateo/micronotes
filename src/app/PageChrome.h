#pragma once

#include "app/PageView.h"
#include "app/Shell.h"

#include <optional>
#include <string>
#include <string_view>

// The application's side of the affordances a `PageView` draws over its text.
//
// A page hands back where its chrome is -- the copy button, the disclosure
// control, the gutter handles -- and knows nothing about what a click on one
// should do, because that is the application's business and not the renderer's.
// This is where those answers live, so `Application.cpp` dispatches rather than
// implements.
namespace micronotes::app {

// The code a page's copy button would copy, or nothing when the pointer is not
// on one. `source` must be the buffer the page was laid out over.
//
// Both surfaces answer it. Reading a note is exactly when somebody wants the
// code out of it, and copying edits nothing -- which is why this is the one
// piece of page chrome a read-only page still carries.
std::optional<std::string> codeUnderCopyButton(const PageView& page, std::string_view source,
                                               float x, float y);

// Follows the link under the pointer, if there is one, and says whether it
// found one. `ui.linkRegions` is what the panes collected this frame, so the
// same call serves whichever of them drew it.
//
// Four kinds of target arrive here and they go four different ways: a
// `[[wikilink]]` opens (or creates) a note, a bare `#anchor` scrolls the page
// showing this one, a URL goes to the desktop, and anything else is a file in
// the library opened with whatever the desktop uses for it.
// The link under the pointer, or null.
//
// One lookup, because there were three: `pointOnLink`, `followLinkAt` and the
// cursor shape each walked `ui.linkRegions` for themselves with their own
// `contains`. Three spellings of "which link is under the pointer" is three
// chances for the cursor to promise a click that lands somewhere else -- the
// same failure `ui::layoutTabs` had, where the hit test and the paint
// disagreed and a click on the second tab opened the first.
const ui::LinkRegion* linkAt(const UiRuntime& ui, float x, float y);

// Whether there is a link under the pointer at all, without following it.
//
// The middle-click handler needs to know before it decides what the click
// meant: on a link it is "open in a new tab", and on text it is an X11
// primary-selection paste.
bool pointOnLink(const UiRuntime& ui, float x, float y);

bool followLinkAt(UiRuntime& ui, float x, float y);

}
