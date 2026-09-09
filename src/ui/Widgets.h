#pragma once

#include "core/editor/TextField.h"
#include "ui/Rect.h"
#include "ui/TextRenderer.h"
#include "ui/Tooltip.h"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

// The compounds a surface assembles itself from: a titled card, a section band,
// a button, a text-field frame, a menu row, a strip tab, an empty state and a
// tooltip.
//
// One layer above `ui/Painter.h` and `ui/Glyphs.h`, built out of both, and the
// layer the surfaces in `src/app/` actually talk to. Every one of these exists
// because the same thing had been drawn two or three ways; the notes on the
// declarations say which, and in every case the copies had drifted -- by a
// pixel, by a type size, or by a whole treatment.
namespace micronotes::ui {

// A text field's frame: the panel ground with a 1px rule round it, accent when
// it has the keyboard.
void drawTextFieldFrame(SDL_Renderer* renderer, Rect box, bool active);

// Where and how a single-line field's contents go inside that frame.
struct TextFieldPaint {
  // The rect the text is laid out and clipped to -- the frame for a framed
  // field, the text line itself for one drawn bare.
  Rect box;
  // Top of the text line. `ui::textTop` is what every centred line in the shell
  // uses; a caller whose box *is* the line passes the line's own top.
  float textY = 0.0f;
  // Inset of the text from the box's left edge.
  float padX = 0.0f;
  // How far the caret and the selection band sit inside the box, top and
  // bottom. Negative to grow them past it, which a bare field wants.
  float insetY = 0.0f;
  // Shown in the muted ink when the field is empty. *Under* the caret, not
  // instead of it.
  std::string_view placeholder;
  bool focused = false;
  // The blink's on-phase, settled once a frame so every caret on screen blinks
  // together. See `ui::CaretBlink`.
  bool caretVisible = true;
};

// A single-line field's contents: the selection band, the text or its
// placeholder, and the caret. Returns where the caret landed, so a caller can
// hand the IME a candidate rectangle; empty when the field has no keyboard.
//
// Three surfaces drew this by hand -- the sidebar's search box, the overlay's
// prompt, the settings card's filter -- and two of the three drew the
// placeholder *instead of* the field rather than under it, so an empty prompt
// had no insertion point at all. A field with no caret is not a text field,
// which is the sentence `editor::SingleLineView` was written to make true, and
// two of its three callers gave it back on the one input that most needs it.
//
// The field is taken by reference because laying it out settles `scrollX`: how
// far a long value has scrolled to keep the caret in sight is carried across
// frames, so the box does not snap back to the left on every repaint.
Rect drawTextFieldText(SDL_Renderer* renderer, TextRenderer& text, const TextStyle& style,
                       editor::TextField& field, const TextFieldPaint& paint);

// A button with its label centred. `tone` picks the fill: a neutral button
// takes the raised ground, a destructive one takes the warning colour.
enum class ButtonTone {
  Neutral,
  Accent,
  Destructive
};

// A button with its label centred, in the chrome face.
void drawButton(SDL_Renderer* renderer, TextRenderer& text, Rect box, std::string_view label,
                bool enabled, bool hovered, ButtonTone tone = ButtonTone::Neutral);

// A panel with a titled header band, and the header's rect back so the caller
// can set the title in it.
//
// The sibling microide's `DrawTitledCardFrame`, and its prompts and overlays
// are all built on it. What it buys is that a dialog stops being "a rectangle
// with some bold text at the top": the header takes the *chrome* ground, so it
// reads as the same kind of surface as the menu bar and the tab strip, and a
// hairline along its foot separates the question from the answer. micronotes
// drew the title as a line of text on the panel's own ground, which is a title
// that looks like the first row of the list under it -- and on a Confirm, where
// the "list" is two buttons, like a stray label.
//
// A header of zero height is a panel with no title, which is what an anchored
// context menu wants.
Rect drawTitledCard(SDL_Renderer* renderer, Rect card, float headerHeight);

// A band across a panel heading the rows under it: NOTEBOOKS, TAGS, RECENT.
//
// A *band*, not a label. These were four words set in the panel's own ground,
// in the panel's own muted ink, on the panel's own label column -- so the
// sidebar read as one long list with some words in it, and where one group
// ended and the next began was something the reader had to infer from the shape
// of the entries. The tree had no heading at all, which made it worse: naming
// three of four groups reads as one unnamed group running into a named one.
//
// The treatment is the sibling microide's, from the git sidebar's Staged /
// Unstaged / Untracked groups, and it is three things at once -- a raised
// ground, a hairline rule along the top, and the label outdented an indent step
// ahead of the rows. Any one of them alone is a hint; the three together are a
// division nobody has to look for.
//
// `chevron` empty means a **caption** rather than a band: a result count or the
// name of the tag being filtered by, which head the list the same way but have
// nothing under them to shut. A caption gets no control and no hover.
// `trailing` is a count, right-aligned, and is worth most on a band that is
// shut -- the one case where what is under it cannot be counted by looking.
// `trailingReserve` is what the band may not paint into at that edge, which is
// the scrollbar's lane when one is showing: see `ui::scrollbarReserve`. The
// band fills its whole width either way -- it is the ground that makes it a
// band -- so this narrows the *count*, not the rect.
void drawSectionBand(SDL_Renderer* renderer, TextRenderer& text, Rect band, Rect chevron,
                     std::string_view label, std::string_view trailing, bool collapsed,
                     bool hovered, float trailingReserve);

// An empty place says what it is, what to do about it, and which keys do that.
// The third line is what turns a dead end into an offer, so it is dimmer than
// the rest rather than left out.
//
// Takes an origin and a width, and **returns the height it used**, because that
// height is not something a caller can know: the detail wraps to the column and
// the key line is optional, so the message is between two and five lines tall
// depending on the text and on the reader's text size. Every caller used to
// pass a rect with a made-up height -- 100, 110, 120 -- which nothing checked
// and nothing read, so at the large text size a three-line empty state ran past
// the box it claimed to be in. Nothing was drawn under any of them, which is
// why nobody noticed; the first surface to put something there would have
// inherited the bug.
float drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail,
                       float x, float y, float width, std::string_view keys = {});

// Draws a resolved tooltip. Called last in a frame, so nothing paints over it.
void drawTooltip(SDL_Renderer* renderer, TextRenderer& text, const HoverTooltip& tooltip, Rect bounds);

// One row of a popup menu: the tick slot, the label, and up to two pieces of
// trailing text against the far edge.
//
// A menu-bar popup, a context menu and the command palette are one object -- a
// card holding a list of commands, each with an accelerator, a tick column and
// a disabled state -- reached through two different item tables. This is what
// the two project into, so the tables stay what they are (one `constexpr`
// static, one runtime vector) and the *row* is one thing.
//
// It was not, and the drift was legible: the tick sat one pixel further right
// in a menu-bar popup than in the palette, and the accelerator was set a whole
// size smaller in the palette than in the menu offering the same command. Both
// are one spelling now, and both follow ../microide's `DrawMenuRow`.
struct MenuRow {
  std::string_view label;
  // The chord. Set in the row's own face, right-aligned, and never ellipsized:
  // a chord with its tail cut off is worse than no chord at all, and the popup
  // was measured to hold it.
  std::string_view accelerator;
  // A second trailing piece, set smaller because it is context rather than a
  // control: the folder a note is in, the moment a deletion happened. Only the
  // overlay's items carry one.
  std::string_view detail;
  bool enabled = true;
  // The pointer is on this row, or the keyboard is. One flag rather than two,
  // because they paint identically and no row is ever both to different effect.
  bool highlighted = false;
  bool checked = false;
  bool destructive = false;
};

void drawMenuRow(SDL_Renderer* renderer, TextRenderer& text, Rect row, const MenuRow& item);

// The rule a separator owns, inset from both edges and down the middle of its
// own (shorter) row. Full width would cut the popup into unrelated cards.
//
// A separator has a row so that the popup's height stays the sum of its rows
// and one walk covers them all -- which is what lets an index into the item
// table and an index into the rows be the same number.
void drawMenuSeparator(SDL_Renderer* renderer, Rect row);

// One tab in a strip: a flat fill, a 2px accent lid when it is the active one,
// the title, and the room its close cross needs kept clear.
//
// The lid is what makes the active tab read as continuous with the page under
// it. The tab used to be rounded at the top and square at the foot, drawn a
// corner taller than the strip so the clip took the bottom corners off -- a
// shape that only works while the strip's ground and the page's differ by
// exactly the right amount.
struct StripTabColors {
  SDL_Color activeFill;
  SDL_Color inactiveFill;
  SDL_Color hoverFill;
  SDL_Color activeText;
  SDL_Color inactiveText;
};

StripTabColors stripTabColors();

void drawStripTab(SDL_Renderer* renderer, TextRenderer& text, Rect rect, std::string_view label,
                  bool active, bool hovered, float closeReserve, const StripTabColors& colors);

// The chevron button at a strip's end, with the number of tabs hidden past it.
// Zero hidden draws nothing at all, so a strip that fits has no furniture.
void drawStripOverflowButton(SDL_Renderer* renderer, TextRenderer& text, Rect box,
                             bool pointRight, std::size_t hidden, bool hovered);

}
