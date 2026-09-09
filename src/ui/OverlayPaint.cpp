#include "ui/Overlay.h"
#include "ui/OverlayStyle.h"

#include "CoreAliases.h"
#include "core/editor/SingleLineView.h"
#include "core/perf/PerformanceCounters.h"

#include "ui/ClipGuard.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/TagColors.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

// Painting the overlay on top of the stack.
//
// Split from `Overlay.cpp` for the reason `PageViewPaint.cpp` is split from
// `PageView.cpp`: the file held the stack's state, its filtering, its geometry,
// its keyboard and mouse handling and its draw, and the draw was a third of it.
// The geometry stays where the hit test can reach it -- `layoutFor` is called by
// both, which is the whole point -- and only the painting moves.

namespace micronotes::ui {
namespace {

// The ring around the cell the keyboard is on: the cursor colour, and a second
// stroke in the panel's own ground just outside it so the ring reads as a ring
// against any swatch it lands on rather than merging into a light one.
void drawHighlightRing(SDL_Renderer* renderer, Rect rect) {
  stroke(renderer, rect, theme().cursor);
  stroke(renderer, {rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f},
         theme().overlayBackground);
}

}

void OverlayStack::draw(SDL_Renderer* renderer, TextRenderer& text, int windowWidth, int windowHeight) {
  // Not const: drawing the field settles its scroll offset, so the next frame
  // and the next hit-test agree with what was painted.
  Overlay* overlay = top();
  if(!overlay) return;

  // Dim whatever is behind so the overlay reads as the focused surface. The
  // renderer blends for its whole life, set once where it is created; this used
  // to turn blending on here and leave it on, which is how a shell that had
  // opened a palette painted differently from one that had not.
  // The palette's own backdrop role, rather than a black wash at a hand-picked
  // alpha per theme: the light theme wants a cooler, lighter dim than the dark
  // one, and picking two numbers here is how the two drifted apart from the
  // rest of the palette.
  // Not every overlay dims. An anchored menu costs one click to dismiss, and an
  // overlay that filters as you type into the note is showing a list *about* the
  // sentence behind it: see `Overlay::dimsBehind`.
  if(overlay->dimsBehind && !overlay->anchored) {
    fill(renderer, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
         theme().overlayBackdrop);
  }

  const auto layout = layoutFor(*overlay, text, windowWidth, windowHeight);
  lastLayout_ = layout;

  // The chrome face, like every other surface that is not the note. A palette
  // set in the text face reads as a document about commands rather than as a
  // list of them, and its accelerators -- which are keys, not words -- have to
  // line up in a column to be scanned at all.
  const TextStyle titleStyle = titleFace();
  const TextStyle bodyStyle = chromeStyle();
  const TextStyle hintStyle = hintFace();

  // The panel, with the header band a titled overlay wears. The title used to
  // be a line of text on the panel's own ground, which is a title that looks
  // like the first row of the list beneath it -- and on a Confirm, whose "list"
  // is two buttons, like a stray label above them.
  const Rect header = drawTitledCard(renderer, layout.panel, layout.title.h);
  if(header.h > 0.0f) {
    text.draw(ellipsizeToWidth(text, overlay->title,
                               static_cast<int>(header.w - kPadding * 2.0f), titleStyle),
              header.x + kPadding, textTop(header, text, titleStyle), theme().chromeText,
              titleStyle);
  }

  if(overlay->takesTypedText()) {
    drawTextFieldFrame(renderer, layout.field, true);
    // `textTop`, like every other centred line in the shell. These four sites
    // each centred by hand and none of them rounded, so the palette's text
    // landed on half pixels and its glyph stems smeared.
    // One painter, shared with the sidebar's search box and the settings card's
    // filter. The prompt used to draw its placeholder *instead of* its field,
    // so an empty prompt -- which every prompt is for its first keystroke --
    // had no insertion point at all.
    TextFieldPaint paint;
    paint.box = layout.field;
    paint.textY = textTop(layout.field, text, bodyStyle);
    paint.padX = kRowPadX;
    paint.insetY = 4.0f;
    paint.placeholder = overlay->placeholder;
    paint.focused = true;
    // The shell settles the blink once a frame so this caret and the page's
    // cannot blink out of step. See `OverlayStack::setCaretVisible`.
    paint.caretVisible = caretVisible_;
    drawTextFieldText(renderer, text, bodyStyle, overlay->value, paint);
  }

  if(overlay->isGrid()) {
    const bool glyphs = overlay->kind == OverlayKind::GlyphPicker;
    for(std::size_t i = 0; i < layout.itemRects.size(); ++i) {
      const auto rect = layout.itemRects[i];
      const int index = layout.itemIndices[i];
      if(index < 0 || index >= static_cast<int>(overlay->items.size())) continue;
      const bool highlighted = index == overlay->highlighted || contains(rect, mouseX_, mouseY_);
      const bool inForce = index == overlay->current;
      if(glyphs) {
        // A glyph cannot fill its cell the way a colour does, so the cell needs
        // a ground of its own or the grid is eleven marks floating on the
        // panel with no edges to point at. The mark takes the accent when it is
        // the one in force, so the answer to "which is it now" is the mark
        // itself rather than a tick sitting on top of one.
        fill(renderer, rect, inForce ? theme().selectionFill : theme().surfaceRaised);
        const SDL_Color ink = inForce ? theme().accent
                                      : (highlighted ? theme().textPrimary : theme().textSecondary);
        // The id of the "no icon" cell is empty, and drawNoteGlyph declines it,
        // which is exactly the fallback the sidebar takes for a note with none.
        const auto& id = overlay->items[static_cast<std::size_t>(index)].id;
        if(!drawNoteGlyph(renderer, id, rect, ink)) {
          fill(renderer, {rect.x + rect.w / 2.0f - 5.0f, rect.y + rect.h / 2.0f, 10.0f, 1.0f}, ink);
        }
        if(highlighted) drawHighlightRing(renderer, rect);
        continue;
      }
      // The swatch fills its cell, so the thing being chosen is the thing being
      // pointed at. A colour shown as a chip inside a row would be competing
      // with the row's own ground for what the eye reads as "this colour".
      fill(renderer, rect, tagSwatch(index));
      // Two marks, and they say different things: a tick for the colour the tag
      // already has, an outline for the one the pointer is on. A picker that
      // shows only one of them cannot answer "what is it now" and "what would
      // this do" at the same time, which is the only question being asked.
      if(inForce) {
        drawCheckGlyph(renderer, {rect.x + rect.w / 2.0f - 6.0f, rect.y + rect.h / 2.0f - 6.0f,
                                  12.0f, 12.0f},
                       theme().onAccent);
      }
      if(highlighted) drawHighlightRing(renderer, rect);
    }
    if(!layout.hint.w) return;
    text.draw(overlay->hint, layout.hint.x, layout.hint.y, theme().textMuted, hintStyle);
    return;
  }

  for(std::size_t i = 0; i < layout.itemRects.size(); ++i) {
    const auto rect = layout.itemRects[i];
    const int index = layout.itemIndices[i];
    if(index < 0) {
      const bool isConfirm = index == -1;
      const bool hot = contains(rect, mouseX_, mouseY_);
      // Confirm wears the destructive colour: every Confirm overlay in the shell
      // asks about a deletion, and a button that is about to delete something
      // should not look like the one beside it that will not.
      drawButton(renderer, text, rect, isConfirm ? overlay->confirmLabel : std::string("Cancel"),
                 true, hot, isConfirm ? ButtonTone::Destructive : ButtonTone::Neutral);
      continue;
    }

    const auto& item = overlay->items[static_cast<std::size_t>(index)];
    if(item.separator) {
      drawMenuSeparator(renderer, rect);
      continue;
    }
    // The same row a menu-bar popup draws. A palette row, a context-menu row
    // and a menu-bar row are the same object reached through two item tables,
    // and this is where the second one projects into it.
    MenuRow row;
    row.label = item.label;
    row.accelerator = item.shortcut;
    row.detail = item.detail;
    row.enabled = item.enabled;
    row.highlighted = index == overlay->highlighted || contains(rect, mouseX_, mouseY_);
    row.checked = item.checked;
    row.destructive = item.destructive;
    drawMenuRow(renderer, text, rect, row);
  }

  // A list taller than the panel says so, or the last visible row would read as
  // the end of the list.
  const auto& filtered = visibleIndices(*overlay);
  if(overlay->hasRows() && !layout.itemRects.empty() &&
     filtered.size() > layout.itemRects.size()) {
    // The shell's scrollbar, not a fifth private one. This drew its own track
    // and thumb at its own inset, its own width and its own minimum height,
    // with no border on the thumb -- so a list that scrolled in the command
    // palette did not look like a list that scrolled anywhere else.
    //
    // `drawVerticalScrollbar` works in pixels and the overlay scrolls in rows,
    // which is a pure scaling: at `pitch` pixels a row, `viewport.h` is the
    // rows on screen and `maxScroll` the rows off it, and both the visible
    // fraction and the thumb's travel come out identical to what was here.
    const float shown = static_cast<float>(layout.itemRects.size());
    const float pitch = (layout.itemRects.back().y + layout.itemRects.back().h - layout.itemRects.front().y) / shown;
    const Rect band {layout.panel.x, layout.itemRects.front().y - kScrollbarInset, layout.panel.w,
                     shown * pitch + kScrollbarInset * 2.0f};
    const int hidden = static_cast<int>(filtered.size() - layout.itemRects.size());
    drawVerticalScrollbar(renderer, band,
                          static_cast<int>(std::lround(static_cast<float>(overlay->rows.scroll) * pitch)),
                          static_cast<int>(std::lround(static_cast<float>(hidden) * pitch)));
  }

  if(!overlay->hint.empty() && layout.hint.h > 0.0f) {
    text.draw(overlay->hint, layout.hint.x, layout.hint.y, theme().textMuted, hintStyle);
  }

  if(overlay->hasRows() && filtered.empty()) {
    // Where the first row would have been. It used to be placed a bare 24
    // pixels up from the panel's foot, which is where the hint is drawn -- so a
    // filterable palette with a hint wrote the two over each other the moment
    // the query matched nothing.
    text.draw("No matches", layout.panel.x + kPadding + kSpace1,
              overlay->takesTypedText() ? layout.field.y + layout.field.h + kPadding
                                  : layout.panel.y + kPadding,
              theme().textMuted, bodyStyle);
  }
}

}
