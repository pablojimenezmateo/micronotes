#pragma once

#include "core/editor/TextField.h"
#include "ui/Draw.h"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace micronotes::ui {

enum class OverlayKind {
  // A single-line text field with a title, e.g. rename or tag editing.
  TextPrompt,
  // A list of choices, optionally filtered as the user types. Backs context
  // menus, the slash menu, and the command palette.
  List,
  // A destructive-action confirmation.
  Confirm,
  // A grid of colour swatches. A list would do the job with one swatch a row,
  // and would be the wrong shape: choosing a colour is comparing colours, and
  // twelve of them in a column are never all in the eye at once.
  ColorPicker,
  // A grid of the marks a note can wear. The same shape as the swatches, for
  // the same reason: choosing a glyph is comparing glyphs.
  GlyphPicker
};

// Whether an overlay lays its items out as a grid rather than as rows. The two
// pickers differ only in what they paint into a cell, and every rule about
// sizing, pointing at and choosing one is shared.
constexpr bool isGridOverlay(OverlayKind kind) {
  return kind == OverlayKind::ColorPicker || kind == OverlayKind::GlyphPicker;
}

struct OverlayItem {
  std::string id;
  std::string label;
  std::string detail;     // secondary line, e.g. a folder path
  std::string shortcut;   // right-aligned hint, e.g. "Ctrl+N"
  bool enabled = true;
  bool destructive = false;
};

struct Overlay {
  OverlayKind kind = OverlayKind::List;
  std::string id;         // identifies the overlay when a result comes back
  std::string title;
  std::string hint;

  // TextPrompt value, or the filter text of a filterable List.
  editor::TextField value;
  std::string placeholder;

  std::vector<OverlayItem> items;
  bool filterable = false;
  int highlighted = 0;
  // First visible row, as a position in the filtered list rather than an item
  // index: the filter reorders items, and a scroll offset that meant an item
  // would jump every time the text changed.
  int scroll = 0;
  // Most rows this overlay wants on screen at once: enough for the whole
  // block-type list before anyone filters it. A menu is better short; a
  // reference list asks for more. Capped by what the window can fit.
  int maxRows = 12;

  std::string confirmLabel = "Confirm";

  // The choice already in force, as an index into `items`, or -1 for none.
  //
  // Distinct from `highlighted`, which is where the pointer or the keyboard is:
  // a picker has to show both at once -- what the tag is now, and what it would
  // become -- and they are only the same on the frame the picker opens.
  int current = -1;

  // Whether dismissing this overlay is worth telling the caller about.
  //
  // A menu that is escaped has simply gone away, and nothing more needs saying.
  // But an overlay that filters as you type has taken your keystrokes: for the
  // wikilink picker, escaping after typing "Some" must leave "Some" in the note
  // rather than swallowing it. Such an overlay comes back with an empty itemId
  // and whatever was typed in `value`.
  bool reportDismissal = false;

  // Anchored overlays hang off a point (context menus); otherwise the overlay
  // is centred horizontally near the top of the window, like a palette.
  bool anchored = false;
  float anchorX = 0.0f;
  float anchorY = 0.0f;
  float width = 340.0f;

  // What the filter last answered, and for which query.
  //
  // Filtering is a fuzzy score against every item's label and detail, a
  // stable_sort of what matched, and two vectors -- and it was run from scratch
  // on every call. The layout calls it, the draw calls it again, and so does
  // every arrow key, every wheel notch and every commit, so one frame of an open
  // "Go to note" palette scored the whole library at least twice to address the
  // twenty rows it can show. The query is the only thing it depends on that
  // moves, and the items are fixed once the overlay is open, so the query is the
  // key. Mutable because filtering is a query in the other sense too: asking
  // which items match has not changed anything a caller can observe.
  mutable std::vector<int> filterCache;
  mutable std::string filterCacheQuery;
  mutable bool filterCacheValid = false;
};

// What the pointer is over in the overlay on top.
//
// The shell used to answer `Pointer` for the whole window whenever any overlay
// was open -- one line, and it meant the text field in a rename box, a tag
// editor or the command palette never showed a text cursor. Those are the
// fields a reader is most likely to be typing into, and every one of them said
// "click me" while they were being typed in.
enum class OverlayCursor {
  // Not over the overlay at all: the click would dismiss it, so it is still a
  // pointer, but the caller may want to say so differently.
  Outside,
  Panel,    // the panel's own ground, which does nothing
  Text,     // its single-line field
  Pointer   // a row, a swatch or a button
};

struct OverlayResult {
  std::string overlayId;
  std::string itemId;   // List/Confirm: the chosen item
  std::string value;    // TextPrompt: the entered text
};

class OverlayStack {
public:
  bool active() const;
  const Overlay* top() const;
  Overlay* top();

  void open(Overlay overlay);
  void close();
  void closeAll();

  // Each handler returns a result once the user commits; nullopt otherwise.
  // `handled` reports whether the overlay consumed the event at all, so the
  // caller knows not to route it to the rest of the application.
  std::optional<OverlayResult> handleKey(SDL_Keycode key, bool ctrl, bool shift, bool& handled);
  bool handleText(const char* input);
  std::optional<OverlayResult> handleClick(float x, float y, bool& handled);
  void handleMotion(float x, float y);
  // Returns whether an overlay took the wheel, so it cannot also scroll the
  // note behind it.
  bool handleWheel(float dy);

  void draw(SDL_Renderer* renderer, TextRenderer& text, int windowWidth, int windowHeight);

  // Which of the overlay's parts is under the pointer, from the geometry the
  // last frame actually painted -- the same rects `handleClick` tests, so the
  // cursor cannot promise a click the overlay will not honour.
  OverlayCursor cursorAt(float x, float y) const;

  // Whether to paint the field's caret this frame. Handed in rather than timed
  // here: the shell settles the blink once per frame so that every caret on
  // screen -- this one and the page's, which are both visible in a split -- is
  // in the same phase.
  void setCaretVisible(bool visible) { caretVisible_ = visible; }

private:
  struct Layout {
    Rect panel;
    // The header band, empty when the overlay has no title. Recorded rather
    // than recomputed at the draw for the reason `hint` is: the layout is the
    // one thing that knows how tall the band came out, and a title placed by
    // arithmetic the layout did not do is a title that can drift off its band.
    Rect title;
    Rect field;
    // Where the hint line goes. Recorded rather than derived at the draw,
    // because it is not always the panel's foot: on a Confirm the hint is the
    // consequence of the button beside it ("This cannot be undone."), so it
    // has to be read *before* the buttons rather than under them.
    Rect hint;
    std::vector<Rect> itemRects;
    std::vector<int> itemIndices;  // into Overlay::items
  };

  Layout layoutFor(const Overlay& overlay, TextRenderer& text, int windowWidth, int windowHeight) const;
  const std::vector<int>& visibleIndices(const Overlay& overlay) const;
  std::optional<OverlayResult> commit();
  void moveHighlight(int delta);
  // A list longer than the panel scrolls to follow the highlight, so arrowing
  // past the last visible row cannot lose it off the bottom.
  void ensureHighlightVisible();
  void resetHighlight();

  std::vector<Overlay> stack_;
  Layout lastLayout_;
  // Rows the last layout actually fitted. Scrolling has to agree with what was
  // drawn, and only the layout knows how tall the window was.
  mutable int lastRows_ = 12;
  float mouseX_ = -1.0f;
  float mouseY_ = -1.0f;
  bool caretVisible_ = true;
};

}
