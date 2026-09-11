#pragma once

#include "core/editor/TextField.h"
#include "ui/TextRenderer.h"
#include "ui/ScrollList.h"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>
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

struct OverlayItem {
  std::string id;
  std::string label;
  std::string detail;     // secondary line, e.g. a folder path
  std::string shortcut;   // right-aligned hint, e.g. "Ctrl+N"
  bool enabled = true;
  bool destructive = false;
  // A tick in the mark column: this is the value in force, or the toggle that
  // is on.
  //
  // The column is already reserved -- every row's label starts at
  // `kMenuPopupLabelInset` so a palette row and a menu row for the same command
  // line up -- but nothing could ask for the tick that column exists for. So the
  // settings list wrote the *word* "current" in the accelerator column instead,
  // where it read as a second value, and a context menu's toggle had to say
  // "Toggle pinned" because it could not show which way it was set.
  bool checked = false;
  // A rule rather than a row. Menus group their items -- the harmless ones, then
  // the one that deletes something -- and a group with no division between it
  // and the next is not a group.
  //
  // Never highlighted, never committed, and given a shorter row of its own, so
  // the panel's height stays the sum of its rows and one rule walks them all.
  // The menu bar's own popups have had these since they were written; the
  // overlay that backs every *context* menu did not, so the two kinds of menu
  // looked unrelated.
  bool separator = false;

  // --- saying which field a row differs from its neighbours in ------------
  //
  // Every menu the overlay backs used to be written as eight-field aggregates
  // in a braced list, so a row's meaning was carried by which of four trailing
  // bools had become a `true`:
  //
  //   {"delete", "Delete", "", "", hasNote, true, false, false}
  //
  // Four of the eight are almost always the same, and the two that carry the
  // meaning -- `destructive` and `checked` -- sat fifth and seventh in a row
  // of identical-looking literals. A designated initializer would say it, and
  // GCC 13 warns on every field such a list leaves to its default, which is
  // the whole point of using one. So the fields are set by name, through
  // these, and the rows read as what they are:
  //
  //   menuItem("delete", "Delete").enabledIf(hasNote).destroys()
  OverlayItem&& enabledIf(bool ok) && {
    enabled = ok;
    return std::move(*this);
  }
  OverlayItem&& ticked(bool on) && {
    checked = on;
    return std::move(*this);
  }
  OverlayItem&& destroys() && {
    destructive = true;
    return std::move(*this);
  }
  OverlayItem&& withKeys(std::string chord) && {
    shortcut = std::move(chord);
    return std::move(*this);
  }
  OverlayItem&& withDetail(std::string line) && {
    detail = std::move(line);
    return std::move(*this);
  }
};

// An ordinary, enabled row. Anything else about it is said by name -- see the
// four above.
inline OverlayItem menuItem(std::string id, std::string label) {
  OverlayItem row;
  row.id = std::move(id);
  row.label = std::move(label);
  return row;
}

// The rule between two groups of rows.
//
// A named function rather than the eight-field aggregate written out, which is
// what it was at each of the fifteen places a context menu divides itself --
// `{"", "", "", "", false, false, false, true}`, four empty strings and four
// bools whose meaning is their position. `ui/Menus.cpp` has had `sep()` beside
// its own table since that table was written; this is the same thing for the
// menus the overlay backs. Every other row in those tables is a designated
// initializer for the same reason: a row differs from its neighbours in one
// named field, not in which of four trailing `false`s became a `true`.
inline OverlayItem menuSeparator() {
  OverlayItem rule;
  rule.enabled = false;
  rule.separator = true;
  return rule;
}

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
  // How far the list is scrolled and how many rows the last layout fitted, as
  // positions in the *filtered* list rather than item indices: the filter
  // reorders items, and an offset that meant an item would jump every time the
  // text changed.
  //
  // On the overlay rather than on the stack. The row count used to live on
  // `OverlayStack`, which made it the count for whichever overlay was laid out
  // last -- so pushing a confirmation over an open palette left the palette
  // clamping its scroll against the confirmation's one row.
  RowStrip rows;
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

  // Whether the window behind is dimmed.
  //
  // Most overlays are a question you have to answer, and dimming what is behind
  // says so. Two are not: the slash menu and the wikilink picker filter as you
  // type *into the note*, so what is behind them is the sentence you are in the
  // middle of writing -- and a wash over it hides the one thing you need in order
  // to choose from the list. The completion popup in the sibling editor is drawn
  // the same way and for the same reason.
  bool dimsBehind = true;

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

  // --- what the layout, the keys, the click and the draw actually ask -------
  //
  // Five kinds, asked about seventeen times: ten `kind == OverlayKind::X`
  // comparisons plus seven calls to an `isGridOverlay` helper, spread across
  // `layoutFor`, `handleKey`, `handleClick` and `draw`, each of which asked
  // more than once. They are `if` chains over an enum rather than switches, so
  // `-Wswitch` covers none of them: a sixth kind meant finding all seventeen by
  // reading, and the compiler helping with none.
  //
  // The questions are named instead, which is a better answer than a behaviour
  // struct or virtual dispatch for five kinds -- and `isGridOverlay` was
  // already one of them, and was already the seven-call half, which is the
  // evidence that this is the direction that works. A new kind answers these
  // four and the sites do not move; and each one says *why* the arm it guards
  // exists, which `kind == OverlayKind::List` never did.

  // Lays its items out as a grid rather than as a column. The two pickers
  // differ only in what they paint into a cell; every rule about sizing,
  // pointing at and choosing one is shared.
  bool isGrid() const {
    return kind == OverlayKind::ColorPicker || kind == OverlayKind::GlyphPicker;
  }

  // Has a column of rows that can outrun the panel, and therefore scrolls, owns
  // a scrollbar, and has something to say when the filter empties it.
  bool hasRows() const { return kind == OverlayKind::List; }

  // Answers with a Cancel and a Confirm along its foot instead of with a list.
  bool hasConfirmButtons() const { return kind == OverlayKind::Confirm; }

  // The highlighted item is the answer. True of a list and of both pickers --
  // the only difference between them is how they are laid out, and leaving the
  // pickers out of this was how the swatch grid came to be a thing you could
  // point at and not choose from.
  bool choosesAnItem() const { return hasRows() || isGrid(); }

  // Takes typed text: a prompt always, a list only when it filters.
  bool takesTypedText() const {
    return kind == OverlayKind::TextPrompt || (hasRows() && filterable);
  }
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

  // The horizontal bands a panel is made of, top to bottom, in pixels. Zero
  // for a band this overlay does not have.
  //
  // One list, and that is the point. The panel's height is the *sum* of these
  // and the placement below it is a *walk* down them, and those were two
  // expressions naming the same eight things. A band in one and not the other
  // is a panel whose box and whose contents disagree, which is a hint drawn
  // over the last row or a row drawn past the panel's foot.
  struct PanelBands {
    float title = 0.0f;
    float field = 0.0f;
    float list = 0.0f;
    float grid = 0.0f;
    // A hint under a grid needs a gap a hint under a list does not: a list row
    // carries its own vertical padding and a swatch is a hard-edged block, so
    // without it the hint sat against the bottom row of colours.
    float gridHintGap = 0.0f;
    float confirm = 0.0f;
    float hint = 0.0f;

    float total() const;
  };

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

  // Where every part of the panel goes, for the window it is drawn in.
  //
  // Public, and a pure function of the overlay and the window: this is the
  // floating-surface rule every other one of these follows -- `findBarLayout`
  // and the status bar's placement are the same shape -- and it is what lets
  // the geometry be tested rather than looked at. Nothing outside composes a
  // `Layout`; the drawing helpers stay members.
  //
  // Not const in `overlay`: the layout is the only thing that knows how many
  // rows the panel held, and that count belongs to the overlay whose list it
  // counted.
  Layout layoutFor(Overlay& overlay, TextRenderer& text, int windowWidth, int windowHeight) const;

private:
  // Shutting the top overlay: Escape and a click outside are the same event
  // here, and the order of what it does matters. See the definition.
  std::optional<OverlayResult> dismissTop();




  // The three pieces `draw` paints, defined in `OverlayPaint.cpp`. Members
  // rather than free helpers because `Layout` is private: the geometry is the
  // layout's to describe, and nobody outside this class should compose one.
  // No `TextRenderer`: a grid draws swatches and marks, never type.
  void drawGrid(SDL_Renderer* renderer, const Overlay& overlay, const Layout& layout) const;
  void drawRows(SDL_Renderer* renderer, TextRenderer& text, const Overlay& overlay,
                const Layout& layout) const;
  // One function because it was two, guarded two different ways for the same
  // draw. See the definition.
  static void drawHint(TextRenderer& text, const Overlay& overlay, const Layout& layout);
  const std::vector<int>& visibleIndices(const Overlay& overlay) const;
  std::optional<OverlayResult> commit();
  void moveHighlight(int delta);
  // A list longer than the panel scrolls to follow the highlight, so arrowing
  // past the last visible row cannot lose it off the bottom.
  void ensureHighlightVisible();
  void resetHighlight();

  std::vector<Overlay> stack_;
  Layout lastLayout_;
  float mouseX_ = -1.0f;
  float mouseY_ = -1.0f;
  bool caretVisible_ = true;
};

// A context menu, ready for its rows: anchored where the press was, with the
// six fields every one of them sets written once instead of six times.
//
// Paired with `openMenu` below, and the pairing is the point. A context menu
// shows all of itself -- the `maxRows` default is a *palette's*, twelve with
// the rest scrolled behind a scrollbar nobody expects on a menu -- so every
// menu set it from its own row count on the line before opening. Every menu
// but one: the tag menu never did, and nothing noticed because it has five
// rows. Setting it where the rows are known rather than at each call site is
// the difference between a rule and a habit.
Overlay anchoredMenu(std::string id, float x, float y, float width);

// Opens a menu built by `anchoredMenu`, showing all of its rows. See above for
// why the row cap is set here rather than by the caller.
void openMenu(OverlayStack& overlays, Overlay overlay);

}
