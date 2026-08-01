#include "core/editor/SingleLineView.h"

#include "core/editor/SingleLineEditor.h"
#include "core/perf/PerformanceCounters.h"
#include "core/util/Utf8.h"

#include <algorithm>
#include <vector>

namespace microcore::editor {
namespace {

// Keeps a few pixels of text visible past the caret so it never sits flush
// against the right edge, which reads as "the text is cut off here".
constexpr float kCaretMargin = 8.0f;

float measureWidth(const TextWidthFn& measure, std::string_view run) {
  if(run.empty()) return 0.0f;
  perf::addCounter(perf::CounterId::EditorSingleLineMeasures);
  return static_cast<float>(measure(run));
}

}

SingleLineView layoutSingleLine(
    const SingleLineEditor& editor,
    float width,
    float scrollX,
    const TextWidthFn& measure) {
  perf::addCounter(perf::CounterId::EditorSingleLineLayouts);

  const std::string_view text = editor.text();
  SingleLineView view;
  view.textWidth = measureWidth(measure, text);
  view.caretX = measureWidth(measure, text.substr(0, std::min(editor.cursor(), text.size())));

  view.hasSelection = editor.hasSelection();
  if(view.hasSelection) {
    view.selectionStartX = measureWidth(measure, text.substr(0, std::min(editor.selectionStart(), text.size())));
    view.selectionEndX = measureWidth(measure, text.substr(0, std::min(editor.selectionEnd(), text.size())));
  }

  // Scroll only as far as the caret demands. Recentring on every keystroke
  // would make the text jump under the cursor while typing.
  const float usable = std::max(1.0f, width);
  view.scrollX = std::max(0.0f, scrollX);
  if(view.caretX - view.scrollX > usable - kCaretMargin) {
    view.scrollX = view.caretX - usable + kCaretMargin;
  }
  if(view.caretX < view.scrollX) view.scrollX = view.caretX;
  // Never leave blank space on the right while text is hidden on the left: once
  // the field is short enough to fit, it must be flush left again.
  view.scrollX = std::clamp(view.scrollX, 0.0f, std::max(0.0f, view.textWidth - usable + kCaretMargin));
  return view;
}

std::size_t offsetAtX(std::string_view text, float x, const TextWidthFn& measure) {
  if(text.empty() || x <= 0.0f) return 0;

  std::vector<std::size_t> boundaries;
  boundaries.reserve(text.size() + 1);
  boundaries.push_back(0);
  for(std::size_t offset = 0; offset < text.size();) {
    offset = util::nextBoundary(text, offset);
    boundaries.push_back(offset);
  }

  // Prefix widths grow monotonically, so the boundary can be found in a
  // logarithmic number of measurements rather than one per character.
  std::size_t low = 0;
  std::size_t high = boundaries.size() - 1;
  while(low < high) {
    const std::size_t mid = (low + high + 1) / 2;
    if(measureWidth(measure, text.substr(0, boundaries[mid])) <= x) low = mid;
    else high = mid - 1;
  }

  // Snap to whichever half of the character the click landed in, which is what
  // clicking the right side of a glyph does in every other text field.
  if(low + 1 < boundaries.size()) {
    const float left = measureWidth(measure, text.substr(0, boundaries[low]));
    const float right = measureWidth(measure, text.substr(0, boundaries[low + 1]));
    if(x - left > right - x) return boundaries[low + 1];
  }
  return boundaries[low];
}

}
