#pragma once

#include "core/util/TextSearch.h"
#include "doc/Layout.h"
#include "ui/TextRenderer.h"
#include "ui/ScrollList.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace micronotes::app {

// The measure and the line height the page lays a note out with, taken
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

// The type scale the page lays out at: the reader's body and mono
// sizes, the six heading sizes, and the leading ratio. Public for the same
// reason `documentMetrics` is -- a harness measuring the real font path has to
// measure it at the sizes the app uses.
doc::TypeMetrics documentTypeMetrics();

// The two things the page cannot do itself: measure and draw a block
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

// The copy button drawn on a fenced code block.
struct PageCodeButton {
  ui::Rect rect;
  std::size_t blockStart = 0;
};

// Everything a page has to be told before it can lay a frame out.
//
// One value handed over in one call, rather than a sequence of setters called
// in an order each caller remembers for itself. The failure mode when one of
// them fell behind was silent and asymmetric: a page that is not told about a
// new revision does not break, it *keeps a stale layout*. That is not
// hypothetical -- the reading pane rendered `[[Some Note]]` as literal brackets
// for as long as it did because it had not been given a wikilink pass.
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
};

// Renders a note as formatted, read-only content: every pixel maps back to a
// byte offset in the buffer, which is what makes the text selectable and its
// links, checkboxes and code buttons clickable.
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

  // Everything this frame's layout depends on, in one call. See `PageFrame`:
  // the sequence used to be a run of setters assembled by hand at the call
  // site, and a page not told about a new revision keeps a stale layout rather
  // than failing.
  void beginFrame(const PageFrame& frame);

  // Lays the note out for this frame. `rect` is the whole content pane.
  void layout(ui::TextRenderer& text, std::string_view source, ui::Rect rect);

  // `findMatches` is the shell's match list, ascending and addressed in buffer
  // bytes -- see `app/FindState.h`. `activeMatch` indexes it, or is
  // `kNoActiveMatch` when there is none to pick out.
  //
  // A list handed in, rather than a query string the page searches for itself,
  // which is what this took. Three surfaces each ran their own scan and so gave
  // three answers to one question: the page cached its matches and the raw pane
  // re-scanned every visible line every frame, and neither could be told to
  // match case because the toggle lived in neither of them.
  static constexpr std::size_t kNoActiveMatch = static_cast<std::size_t>(-1);
  void draw(SDL_Renderer* renderer, ui::TextRenderer& text, const PageSelection& selection,
            bool focused, std::span<const microcore::util::TextMatch> findMatches = {},
            std::size_t activeMatch = kNoActiveMatch);

  std::size_t offsetAt(float x, float y) const;
  // Empty when no link is under the point.
  std::string linkAt(float x, float y) const;
  // Start offset of the task block whose checkbox is under the point.
  std::optional<std::size_t> checkboxAt(float x, float y) const;
  // Start offset of the code block whose copy button is under the point.
  std::optional<std::size_t> copyButtonAt(float x, float y) const;

  // Where the room `PageFrame::headerHeight` asked for ended up this frame, in
  // window coordinates. Moves with the scroll, which is the point.
  ui::Rect headerRect() const;

  // Where an in-note `[#heading]` link or a footnote reference lands, as a
  // scroll offset, or nothing when the note has no anchor by that name. Built
  // once per buffer: the note's headings and its footnote definitions, keyed by
  // the slug `doc::headingAnchor` makes of them.
  std::optional<int> anchorScroll(std::string_view anchor) const;

  // Scrolls `offset` into view. Not "the caret" -- this page has none; what
  // gets revealed is a position in the buffer, which is what stepping through
  // find matches needs.
  void revealOffset(std::size_t offset);
  int scroll() const;
  void setScroll(int value);
  // An offset for the layout that has not happened yet; see
  // `ui::ScrollList::restore`.
  void restoreScroll(int value);
  int maxScroll() const;
  // One wheel event. The page owns its accumulator for the same reason it owns
  // its ceiling: a remainder kept by the caller is a remainder the caller has
  // to remember to keep.
  void wheel(float notches, float pixelsPerNotch);
  ui::Rect pageRect() const;
  // Where the note's own text starts: the content column, at the top of the
  // page. What an empty note's placeholder has to line up with, so it lands
  // where the note's first block would have been.
  ui::Rect columnRect() const;

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
  // What a block's paint needs that does not change from one block to the next.
  struct BlockPaint {
    float ox = 0.0f;
    float oy = 0.0f;
    float viewTop = 0.0f;
    float viewBottom = 0.0f;
  };
  bool drawBlock(SDL_Renderer* renderer, ui::TextRenderer& text, std::size_t index,
                 const BlockPaint& paint, std::size_t& runs);
  void drawBlockDecorations(SDL_Renderer* renderer, ui::TextRenderer& text);
  // The find matches, banded to the blocks on screen. Kept out of `draw`
  // because the interesting part is what it does *not* do: it builds a rect
  // only for the matches inside the window, which is a pair of binary searches
  // over a list that is already sorted.
  void drawFindHighlights(SDL_Renderer* renderer, std::span<const microcore::util::TextMatch> matches,
                          std::size_t activeMatch, std::size_t firstBlock, std::size_t lastBlock,
                          float ox, float oy);
  // The language label and copy button, drawn over a code block's first line.
  void drawCodeChrome(SDL_Renderer* renderer, ui::TextRenderer& text);
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
  std::vector<PageLink> links_;
  std::vector<PageCheckbox> checkboxes_;
  std::vector<PageCodeButton> codeButtons_;
  bool wired_ = false;
  std::uint64_t wikiLinkRevision_ = 0;
  std::uint64_t imageRevision_ = 0;
  std::uint64_t sourceRevision_ = 0;
  editor::TextEdit editedSpan_;
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
  float pointerX_ = -1.0f;
  float pointerY_ = -1.0f;
};

}
