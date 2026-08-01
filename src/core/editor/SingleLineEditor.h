#pragma once

#include "core/editor/MarkdownEditor.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace microcore::editor {

// A one-line text field model: caret, selection, word motion, and undo.
//
// Both apps used to store every side input -- search, find, tag, rename -- as a
// bare std::string with two operations: append on SDL_EVENT_TEXT_INPUT, and
// `pop_back()` on Backspace. That is worse than an HTML <input> in three ways
// that a user notices immediately:
//
//  * `pop_back()` erases one *byte*. Backspacing over "café" or any CJK
//    character left a truncated sequence behind, which renders as a replacement
//    glyph and can no longer round-trip through the file it is about to name.
//  * There was no caret and no motion. The insertion point was always the end
//    of the string, so fixing a typo in the middle meant deleting everything
//    after it. Arrows, Home/End and word motion did nothing at all.
//  * "Selection" was a single `bool inputAllSelected`, so the only selectable
//    range was the whole field. Shift+Arrow, drag-select and partial cut had no
//    representation to work with.
//
// The motion, UTF-8 and undo rules are already correct in MarkdownEditor, so
// this composes it rather than restating them; the single-line part is the
// newline filtering and the Home/End mapping.
class SingleLineEditor {
public:
  SingleLineEditor() = default;
  explicit SingleLineEditor(std::string value);

  // Replaces the contents and puts the caret at the end, the way focusing a
  // field pre-filled with an existing value behaves. Clears undo history: the
  // previous field's edits are not something Ctrl+Z should walk back into.
  void setText(std::string value);
  void clear();

  // --- editing ---
  // Newlines are flattened to spaces. A single-line field has nowhere to put
  // one, and keeping it would let a pasted paragraph smuggle a line break into
  // a note title or a file name.
  void insert(std::string_view value);
  void erasePrevious();
  void eraseNext();
  void eraseSelection();
  void eraseWordBefore();
  void eraseWordAfter();

  // --- caret and selection ---
  void moveCursor(std::size_t cursor);
  void moveLeft(bool keepSelection = false);
  void moveRight(bool keepSelection = false);
  void moveWordLeft(bool keepSelection = false);
  void moveWordRight(bool keepSelection = false);
  void moveHome(bool keepSelection = false);
  void moveEnd(bool keepSelection = false);

  void selectRange(std::size_t anchor, std::size_t cursor);
  void selectAll();
  void clearSelection();
  bool hasSelection() const;
  std::size_t selectionStart() const;
  std::size_t selectionEnd() const;
  std::string selectedText() const;

  // --- history ---
  bool undo();
  bool redo();

  // --- state ---
  const std::string& text() const;
  std::size_t cursor() const;
  bool empty() const;

private:
  MarkdownEditor editor_;
};

// Replaces every CR/LF with a space. Exposed for the callers that need to
// sanitise a value before it reaches a field.
std::string flattenToSingleLine(std::string_view value);

}
