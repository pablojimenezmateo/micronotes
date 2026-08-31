#pragma once

#include "doc/Layout.h"
#include "ui/Draw.h"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::app {

// The two things the live surface cannot do itself: measure and draw a block
// the scanner deliberately does not model. Supplied by the application, which
// owns the md4c render model.
struct PageViewHooks {
  std::function<float(const doc::SourceBlock&, float width)> measureComplex;
  std::function<void(const doc::SourceBlock&, ui::Rect)> drawComplex;
};

struct PageLink {
  ui::Rect rect;
  std::string target;
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

// Renders a note as formatted, editable content: the caret is a byte offset in
// the buffer and every pixel maps back to one.
class PageView {
public:
  void setHooks(PageViewHooks hooks);

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

  void setFolds(PageFolds folds);

  // Per-frame view state, set before `draw`.
  void setPointer(float x, float y);
  void setBlockSelection(PageBlockSelection selection);
  void setDropOffset(std::optional<std::size_t> offset);
  // Suppresses the toolbar while a click is still being dragged into a
  // selection, so it cannot land under the pointer mid-drag.
  void setSelecting(bool selecting);
  std::size_t rowRelative(std::size_t offset, int deltaRows) const;
  std::size_t rowsPerPage() const;

  void revealCaret(std::size_t offset);
  int scroll() const;
  void setScroll(int value);
  int maxScroll() const;
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
  const std::vector<PageLink>& links() const;

private:
  float originX() const;
  float originY() const;
  // Rect of the whole block in window coordinates.
  ui::Rect blockRect(std::size_t index) const;
  // Backgrounds and rules, drawn under the text of every visible block.
  void drawBlockDecorations(SDL_Renderer* renderer, ui::TextRenderer& text);
  // The language label and copy button, drawn over a code block's first line.
  void drawCodeChrome(SDL_Renderer* renderer, ui::TextRenderer& text);
  void drawFoldControls(SDL_Renderer* renderer);
  void drawGutter(SDL_Renderer* renderer, ui::TextRenderer& text);
  void drawDropIndicator(SDL_Renderer* renderer);
  void drawToolbar(SDL_Renderer* renderer, ui::TextRenderer& text, const PageSelection& selection);

  doc::DocumentLayout document_;
  PageViewHooks hooks_;
  ui::TextRenderer* text_ = nullptr;
  float metricsScale_ = -1.0f;
  ui::Rect rect_ {};
  ui::Rect page_ {};
  float columnLeft_ = 0.0f;
  float columnWidth_ = 640.0f;
  float contentTop_ = 0.0f;
  int scroll_ = 0;
  std::optional<std::size_t> rawOffset_;
  std::vector<PageLink> links_;
  std::vector<PageCheckbox> checkboxes_;
  std::vector<PageToolbarButton> toolbar_;
  std::vector<PageGutterHit> gutter_;
  std::vector<PageFoldHit> foldHits_;
  std::vector<PageCodeButton> codeButtons_;
  PageFolds folds_;
  PageBlockSelection blockSelection_;
  std::optional<std::size_t> dropOffset_;
  float pointerX_ = -1.0f;
  float pointerY_ = -1.0f;
  bool selecting_ = false;
};

}
