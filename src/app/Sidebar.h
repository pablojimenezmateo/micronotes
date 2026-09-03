#pragma once

#include "app/Shell.h"
#include "core/editor/TextField.h"
#include "ui/Draw.h"
#include "ui/Rect.h"

#include <cstddef>
#include <string_view>

#include <SDL3/SDL.h>

// The sidebar: its geometry, its paint, and the single-line field it carries.
//
// Split out of Application.cpp, which is being decomposed. The model half --
// what rows exist and where they sit -- is next door in SidebarModel; this is
// the part that touches a renderer, plus the rects that the paint and the input
// handling both have to derive from one place or a click lands somewhere other
// than where the glyph it pointed at was drawn.
namespace micronotes::app {

// The search field sits on top of the navigation it filters, inside the
// sidebar, because filtering and browsing are the same question asked two ways
// and they used to be in two panels either side of a divider.
ui::Rect searchBoxRect(ui::Rect sidebar);
// What sits inside the search box, laid out from the label's measured width so
// the three parts cannot overlap at any text size. One geometry, read by the
// paint and by the hit test.
struct SearchBoxParts {
  ui::Rect label;
  ui::Rect field;
  ui::Rect scope;
};
SearchBoxParts searchBoxParts(ui::Rect sidebar, const ui::TextRenderer& text);

// The strip inside the search box that holds the text: after the "Find" label
// and before the scope toggle.
ui::Rect searchTextRect(ui::Rect sidebar, const ui::TextRenderer& text);
// Everything below the search field: the scrolling row list, which is the only
// thing buildSidebarRows() and sidebarRowAt() ever measure against.
ui::Rect sidebarListRect(ui::Rect sidebar);

// Paints a single-line field: selection band, then the text scrolled so the
// caret is visible, then the caret.
void drawTextField(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                   editor::TextField& field, ui::Rect box, bool focused,
                   std::string_view placeholder);
// Byte offset in `field` under a pointer at window x, for a field drawn in
// `box`. Always a code point boundary.
std::size_t fieldOffsetAtX(const ui::TextRenderer& text, const editor::TextField& field,
                           ui::Rect box, float x);

void drawSidebar(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, ui::Rect rect);

}
