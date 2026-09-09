#pragma once

#include "doc/Layout.h"
#include "ui/TextRenderer.h"
#include "ui/ScrollList.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace micronotes::app {

// The measure and the line height the live surface lays a note out with, taken
// from a real font. `measureComplex` is left unset: only a caller that owns the
// md4c render model can answer it.
//
// Public because the perf harness needs exactly these. Every budget in
// `tools/PerfMain.cpp` used to run against a fixed-advance stand-in, so the
// largest cost in the app -- glyph shaping to open a note -- was invisible to
// `run-checks.sh perf` and only showed up in a real session.
//
// The renderer is captured by pointer and must outlive the metrics, which for
// the one in the app means the process.
doc::Metrics documentMetrics(ui::TextRenderer& text);

// The type scale the live surface lays out at: the reader's body and mono
// sizes, the six heading sizes, and the leading ratio. Public for the same
// reason `documentMetrics` is -- a harness measuring the real font path has to
// measure it at the sizes the app uses.
doc::TypeMetrics documentTypeMetrics();

// The two things the live surface cannot do itself: measure and draw a block
// the scanner deliberately does not model. Supplied by the application, which
// owns the md4c render model.
struct PageViewHooks {
  std::function<float(const doc::SourceBlock&, float width)> measureComplex;
  std::function<void(const doc::SourceBlock&, ui::Rect)> drawComplex;
  // Whether a `[[target]]` names a note that exists. The page has a buffer, not
  // a library, so it asks; unset means "assume it does".
  std::function<bool(std::string_view)> wikiLinkResolves;
  // The two halves of a picture. The page has a buffer, not a texture cache, so
  // it asks for the box and hands the drawing back. Leaving both unset is what
  // a surface that does not want pictures does.
  std::function<doc::ImageBox(std::string_view target, float column, float maxHeight)> measureImage;
  std::function<void(std::string_view target, ui::Rect)> drawImage;
};

struct PageLink {
  ui::Rect rect;
  std::string target;
  // A [[wikilink]] names a note; every other link names a file or a URL. They
  // look the same as strings and are followed in completely different ways, so
  // which one it is travels with the rect rather than being guessed at from the
  // target's shape.
  bool wiki = false;
};

// A task checkbox the user can click. `blockStart` addresses the owning block.
struct PageCheckbox {
  ui::Rect rect;
  std::size_t blockStart = 0;
};

struct PageSelection {
  std::size_t start = 0;
  std::size_t end = 0;
};

// Whole blocks selected as objects rather than as text. Held as source offsets,
// not block indices, so an edit underneath cannot silently re-point it.
struct PageBlockSelection {
  bool active = false;
  std::size_t anchor = 0;
  std::size_t focus = 0;
};

// A hover affordance in the left gutter: the drag handle, or the button that
// inserts a block below.
struct PageGutterHit {
  ui::Rect rect;
  std::size_t blockIndex = 0;
  std::size_t blockStart = 0;
  bool insert = false;
};

// How the view learns which toggles are collapsed, and how it says it had to
// expand one. Both talk in blocks rather than offsets, so an edit that moves a
// heading cannot detach its fold or apply it to whatever landed there instead.
struct PageFolds {
  std::function<bool(const doc::SourceBlock&)> collapsed;
  std::function<void(const doc::SourceBlock&)> expand;
};

// A toggle's disclosure control in the gutter. `folded` is its state now, so
// the caller does not have to look it up again to know what a click means.
struct PageFoldHit {
  ui::Rect rect;
  std::size_t blockIndex = 0;
  std::size_t blockStart = 0;
  bool folded = false;
};

// The copy button drawn on a fenced code block.
struct PageCodeButton {
  ui::Rect rect;
  std::size_t blockStart = 0;
};

// One button on the floating formatting toolbar. `id` is what the application
// dispatches on.
struct PageToolbarButton {
  ui::Rect rect;
  std::string id;
  std::string label;
};

// Everything a page has to be told before it can lay a frame out.
//
// One value handed over in one call, rather than eleven setters called in an
// order each caller remembers for itself. `drawLive` and `drawReading` opened
// with the same feeding sequence -- wiki revision, image revision, source
// revision, edited span, pointer, header height -- assembled by hand at both,
// and the failure mode when one of them fell behind was silent and asymmetric:
// a page that is not told about a new revision does not break, it *keeps a
// stale layout*. That is not hypothetical. The reading pane rendered
// `[[Some Note]]` as literal brackets for as long as it did because it had not
// been given the wikilink pass the live surface had.
//
// The defaults are the other half of it. Every field here is a decision, and a
// surface that does not make one gets the decision written down rather than
// whatever it happened to leave behind from the frame before.
struct PageFrame {
  // The *editor's* revision, not the page's.
  //
  // The page stamps its layouts with `revision + 1`, because zero is its
  // "cannot say" -- so a caller's number has to be shifted into that space
  // before the layout can check it. Both pages applied the `+1` themselves,
  // which is two copies of an off-by-one; `editedSpan` beside it was already
  // shifted here rather than there, and now they agree.
  std::uint64_t sourceRevision = 0;
  // Where the last edit landed, in the same space. It lets the layout bound the
  // comparison it would otherwise make over the whole note to find the edit; a
  // stamp that does not line up with what the layout holds is discarded there,
  // so a stale one costs the comparison rather than the answer.
  editor::TextEdit editedSpan;

  // A stamp that moves whenever the fold predicate would answer differently.
  // Constant by default, which is exactly what tells the layout that a surface
  // with no folds in it -- the reading pane -- never has to be re-asked.
  std::uint64_t foldRevision = 1;
  // Whether *this note* has anything collapsed. Per frame rather than folded
  // into the predicate, because the layout skips resolving folds entirely when
  // it is handed none, and that is a stronger statement than a predicate that
  // always answers false.
  bool foldsActive = false;

  // The two hooks whose answers are not in a block's own bytes, stamped: a link
  // that starts or stops resolving, a picture that finishes loading. Without
  // them a cached block keeps the colour or the height it was built with.
  std::uint64_t wikiLinkRevision = 0;
  std::uint64_t imageRevision = 0;

  // Room reserved above the note's first block. Measured before the layout
  // because it is scrolling space the page has to reserve, not a banner the
  // note passes under.
  float headerHeight = 0.0f;

  // Off the page by default: a surface nobody is pointing at has no hover.
  float pointerX = -1.0f;
  float pointerY = -1.0f;

  // The four a reading pane leaves alone, and which are most of what makes the
  // live surface live.
  PageBlockSelection blockSelection;
  std::optional<std::size_t> dropOffset;
  // Suppresses the toolbar while a click is still being dragged into a
  // selection, so it cannot land under the pointer mid-drag.
  bool selecting = false;
  bool caretVisible = true;
};

// Renders a note as formatted, editable content: the caret is a byte offset in
// the buffer and every pixel maps back to one.
class PageView {
public:
  // Installed once, not per frame. The hooks' captures -- the renderer, the
  // text renderer, the runtime -- do not change for the life of the process,
  // and each closure is larger than a `std::function`'s inline buffer, so
  // rebuilding them per frame was three heap allocations and three frees on a
  // frame that draws several hundred runs. `wired()` is how the caller knows
  // whether it has done it yet.
  void setHooks(PageViewHooks hooks);
  bool wired() const;
  // Reading rather than editing. The caret, the hover gutter and the selection
  // toolbar are the whole of what an editable surface adds, and all three are
  // already conditional on focus or on the pointer -- so the reading pane is
  // this page with them turned off, rather than a second renderer for the same
  // Markdown. Markers stay hidden whatever the caret says.
  void setReadOnly(bool readOnly);

  // Everything this frame's layout depends on, in one call. See `PageFrame`:
  // the sequence used to be eleven setters, assembled by hand at each of the
  // two surfaces, and a page not told about a new revision keeps a stale layout
  // rather than failing.
  void beginFrame(const PageFrame& frame);

  // Lays the note out for this frame. `rect` is the whole content pane.
  void layout(ui::TextRenderer& text, std::string_view source, std::size_t caret, ui::Rect rect);

  void draw(SDL_Renderer* renderer, ui::TextRenderer& text, std::size_t caret, const PageSelection& selection,
            bool focused, std::string_view findQuery);

  std::size_t offsetAt(float x, float y) const;
  std::optional<std::size_t> blockAt(float x, float y) const;
  // Empty when no link is under the point.
  std::string linkAt(float x, float y) const;
  // Start offset of the task block whose checkbox is under the point.
  std::optional<std::size_t> checkboxAt(float x, float y) const;
  // The gutter affordance under the point, if the pointer is on one.
  std::optional<PageGutterHit> gutterAt(float x, float y) const;
  // Id of the toolbar button under the point, empty when there is none.
  std::string toolbarAt(float x, float y) const;
  // The block boundary a dragged block would drop at, as a source offset.
  std::size_t dropOffsetAt(float y) const;
  // The disclosure control under the point, if the pointer is on one.
  std::optional<PageFoldHit> foldAt(float x, float y) const;
  // Start offset of the code block whose copy button is under the point.
  std::optional<std::size_t> copyButtonAt(float x, float y) const;

  // Also installed once. Whether the *current* note has anything collapsed is
  // per-frame state instead -- `PageFrame::foldsActive` -- because the layout
  // skips resolving folds entirely when it is handed no predicate, which is a
  // stronger statement than a predicate that always answers false. So the
  // predicate stays installed and the frame decides whether it is passed on.
  void setFolds(PageFolds folds);

  // Where the room `PageFrame::headerHeight` asked for ended up this frame, in
  // window coordinates. Moves with the scroll, which is the point.
  ui::Rect headerRect() const;

  std::size_t rowRelative(std::size_t offset, int deltaRows) const;
  std::size_t rowsPerPage() const;

  // Where an in-note `[#heading]` link or a footnote reference lands, as a
  // scroll offset, or nothing when the note has no anchor by that name. Built
  // once per buffer: the note's headings and its footnote definitions, keyed by
  // the slug `doc::headingAnchor` makes of them.
  std::optional<int> anchorScroll(std::string_view anchor) const;

  void revealCaret(std::size_t offset);
  int scroll() const;
  void setScroll(int value);
  int maxScroll() const;
  // One wheel event. The page owns its accumulator for the same reason it owns
  // its ceiling: a remainder kept by the caller is a remainder the caller has
  // to remember to keep.
  void wheel(float notches, float pixelsPerNotch);
  ui::Rect pageRect() const;
  // Where the note's own text starts: the content column, at the top of the
  // page. What an empty note's placeholder has to line up with, so the first
  // character typed appears exactly where the prompt was.
  ui::Rect columnRect() const;

  // A `Complex` block the user clicked into is shown as raw source until the
  // caret leaves it.
  void setRawOffset(std::optional<std::size_t> offset);
  std::optional<std::size_t> rawOffset() const;

  const doc::DocumentLayout& document() const;
  // The block partition of the note as this page last laid it out, when it still
  // describes the buffer stamped `sourceRevision`; empty otherwise. What the
  // block edits borrow instead of scanning the note for themselves -- see
  // `doc::Edits.h`.
  doc::BlockSpan blocksAt(std::uint64_t sourceRevision) const;
  const std::vector<PageLink>& links() const;

private:
  // Hand the page rect, the header and the document extent to the scroll, which
  // is what turns them into a ceiling. Called at the end of `layout()`.
  void recordScrollExtent();
  float originX() const;
  float originY() const;
  // Rect of the whole block in window coordinates.
  ui::Rect blockRect(std::size_t index) const;
  // Half-open range of block indices this frame has to consider, which is the
  // viewport's worth rather than the document's.
  std::pair<std::size_t, std::size_t> visibleBlocks() const;
  // Backgrounds and rules, drawn under the text of every visible block.
  void drawBlockDecorations(SDL_Renderer* renderer, ui::TextRenderer& text);
  // The find query's matches, banded to the blocks on screen. Kept out of
  // `draw` because the interesting part is what it does *not* do: it does not
  // search the note unless the note or the query moved, and it does not build a
  // rect for a match the window cannot show.
  void drawFindHighlights(SDL_Renderer* renderer, std::string_view findQuery, std::size_t firstBlock,
                          std::size_t lastBlock, float ox, float oy);
  // The language label and copy button, drawn over a code block's first line.
  void drawCodeChrome(SDL_Renderer* renderer, ui::TextRenderer& text);
  void drawFoldControls(SDL_Renderer* renderer);
  void drawGutter(SDL_Renderer* renderer, ui::TextRenderer& text);
  void drawDropIndicator(SDL_Renderer* renderer);
  void drawToolbar(SDL_Renderer* renderer, ui::TextRenderer& text, const PageSelection& selection);
  void buildAnchors() const;

  doc::DocumentLayout document_;
  PageViewHooks hooks_;
  ui::TextRenderer* text_ = nullptr;
  float metricsScale_ = -1.0f;
  ui::Rect rect_ {};
  ui::Rect page_ {};
  float columnLeft_ = 0.0f;
  float columnWidth_ = 640.0f;
  float contentTop_ = 0.0f;
  float headerHeight_ = 0.0f;
  // The offset, its ceiling and the wheel. The ceiling is recorded at the end
  // of `layout()` -- the one place the page rect, the header height and the
  // document extent all settle -- so a wheel or a scrollbar drag clamps against
  // what was actually laid out.
  ui::ScrollList scroll_;
  std::optional<std::size_t> rawOffset_;
  std::vector<PageLink> links_;
  std::vector<PageCheckbox> checkboxes_;
  std::vector<PageToolbarButton> toolbar_;
  std::vector<PageGutterHit> gutter_;
  std::vector<PageFoldHit> foldHits_;
  std::vector<PageCodeButton> codeButtons_;
  PageFolds folds_;
  bool wired_ = false;
  bool foldsActive_ = false;
  std::uint64_t wikiLinkRevision_ = 0;
  std::uint64_t imageRevision_ = 0;
  bool readOnly_ = false;
  bool caretVisible_ = true;
  std::uint64_t sourceRevision_ = 0;
  std::uint64_t foldRevision_ = 0;
  editor::TextEdit editedSpan_;
  // Where the find query matches, found once and kept until the query or the
  // buffer moves. Recomputing it per frame made an open find bar cost a pass
  // over the note at frame rate; drawing all of it made the highlight cost the
  // document rather than the window.
  std::vector<std::size_t> findMatches_;
  // The note's anchors, keyed by slug. `mutable` because resolving one is
  // logically a query; rebuilt when the buffer's stamp moves, and every frame
  // for a caller that offers no stamp -- the same contract the find cache has.
  mutable std::map<std::string, float, std::less<>> anchors_;
  mutable std::uint64_t anchorRevision_ = 0;
  mutable bool anchorsValid_ = false;
  // Scratch for the selection and find-highlight rects. A member because both
  // are per-frame calls and a vector returned by value is an allocation and a
  // free on every one of them -- for the find highlighter, one per match on
  // screen.
  mutable std::vector<doc::Rect> selectionRects_;
  std::string findMatchQuery_;
  std::uint64_t findMatchRevision_ = 0;
  bool findMatchesValid_ = false;
  PageBlockSelection blockSelection_;
  std::optional<std::size_t> dropOffset_;
  float pointerX_ = -1.0f;
  float pointerY_ = -1.0f;
  bool selecting_ = false;
};

}
