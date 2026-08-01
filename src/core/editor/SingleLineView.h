#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace microcore::editor {

class SingleLineEditor;

// Measures the pixel width of a UTF-8 run in the font the field is drawn with.
// The model deliberately knows nothing about fonts, so the host passes its
// renderer's measurement in.
using TextWidthFn = std::function<int(std::string_view)>;

// Where a single-line field's caret, selection band and horizontal scroll sit,
// in pixels relative to the field's text origin.
//
// This exists because a field with no caret is not a text field. Both apps drew
// the draft string and nothing else, so there was no insertion point to see, no
// selection to see, and text longer than the box simply vanished past the right
// edge with no way to scroll to it.
struct SingleLineView {
  // Pixels of text hidden past the left edge. Carried across frames so the
  // field does not snap back to the left every time it repaints.
  float scrollX = 0.0f;
  // Caret offset from the text origin, before scrollX is subtracted.
  float caretX = 0.0f;
  float selectionStartX = 0.0f;
  float selectionEndX = 0.0f;
  float textWidth = 0.0f;
  bool hasSelection = false;
};

// Recomputes the view for a field `width` pixels wide, scrolling as far as it
// must to keep the caret visible and no further.
SingleLineView layoutSingleLine(
    const SingleLineEditor& editor,
    float width,
    float scrollX,
    const TextWidthFn& measure);

// Byte offset of the code point boundary nearest `x` pixels from the text
// origin. Always lands on a boundary, so a click can never split a character.
std::size_t offsetAtX(std::string_view text, float x, const TextWidthFn& measure);

}
