#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace microcore::editor {

// A plain-text editing model over a UTF-8 buffer, addressed by byte offset.
//
// Two things here were the source of most of the editor's clumsiness and are
// worth stating explicitly, because both look correct at a glance:
//
//  * Cursor motion used to step one *byte* at a time, so arrowing through or
//    backspacing over any non-ASCII character split it and produced mojibake.
//    All motion now moves whole code points (see core/util/Utf8.h).
//
//  * Undo used to push a full copy of the document on every keystroke, with no
//    coalescing, no bound, and no record of where the caret was. Typing a
//    paragraph into a 100 KB note therefore retained megabytes of near-identical
//    snapshots, Ctrl+Z walked back one character at a time, and each step left
//    the caret wherever it happened to be rather than where the edit was.
//    Records now coalesce over a run of typing, are bounded, and restore the
//    selection they were taken with.
class MarkdownEditor {
public:
  void setText(std::string text);

  // --- editing ---
  void insert(std::string_view text);
  // The single entry point for structural edits. Block transforms, Markdown
  // typing shortcuts and formatting commands all land here so they share the
  // one undo stack instead of keeping parallel state.
  void replaceRange(std::size_t start, std::size_t end, std::string_view text);
  void erasePrevious();
  void eraseNext();
  void erasePreviousWord();
  void eraseNextWord();
  void moveCursor(std::size_t cursor);
  void moveTo(std::size_t offset, bool keepSelection);
  void selectRange(std::size_t anchor, std::size_t cursor);
  void selectAll();
  void clearSelection();
  bool hasSelection() const;
  std::size_t selectionAnchor() const;
  std::size_t selectionStart() const;
  std::size_t selectionEnd() const;
  std::string selectedText() const;
  void eraseSelection();
  void moveLeft(bool keepSelection = false);
  void moveRight(bool keepSelection = false);
  void moveWordLeft(bool keepSelection = false);
  void moveWordRight(bool keepSelection = false);
  void moveLineUp(bool keepSelection = false);
  void moveLineDown(bool keepSelection = false);
  void moveLineStart(bool keepSelection = false);
  void moveLineEnd(bool keepSelection = false);
  void moveDocumentStart(bool keepSelection = false);
  void moveDocumentEnd(bool keepSelection = false);
  // Byte offset of the start of the word before / after `offset`.
  std::size_t wordStartBefore(std::size_t offset) const;
  std::size_t wordEndAfter(std::size_t offset) const;
  bool undo();
  bool redo();
  // Ends the open typing run, so the next edit starts a fresh undo step.
  // Called on focus changes, saves, and anything structural.
  void breakUndoGroup();
  const std::string& text() const;
  std::size_t cursor() const;
  bool dirty() const;
  void markDirty();
  void markSaved();

  // Retained undo bytes and record count, exposed for tests and the harness.
  std::size_t undoBytes() const;
  std::size_t undoDepth() const;

private:
  // Consecutive edits of the same kind at the same spot fold into one undo
  // step; anything else opens a new one.
  enum class EditKind { Structural, Insert, Erase };

  // The selection rides along with the text so Ctrl+Z puts the caret and the
  // highlight back where they were, not just the characters.
  struct Snapshot {
    std::string text;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    bool selecting = false;
  };

  void snapshot(EditKind kind);
  void closeEdit();

  std::string text_;
  std::size_t cursor_ = 0;
  std::size_t selectionAnchor_ = 0;
  bool selecting_ = false;
  bool dirty_ = false;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  EditKind groupKind_ = EditKind::Structural;
  bool groupOpen_ = false;
  std::size_t groupEnd_ = 0;
  std::chrono::steady_clock::time_point groupAt_ {};
};

}
