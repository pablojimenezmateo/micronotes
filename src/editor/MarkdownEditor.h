#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::editor {

class MarkdownEditor {
public:
  void setText(std::string text);
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

private:
  // Consecutive edits of the same kind at the same spot fold into one undo
  // step; anything else opens a new one.
  enum class EditKind { Structural, Insert, Erase };

  struct Snapshot {
    std::string text;
    std::size_t cursor = 0;
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
