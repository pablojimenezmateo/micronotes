#include "editor/MarkdownEditor.h"

#include <algorithm>
#include <cctype>

namespace micronotes::editor {
namespace {

constexpr std::size_t kMaxUndoSnapshots = 100;
// Typing pauses longer than this start a new undo step, so a burst of keys is
// one Ctrl+Z but a considered edit minutes later is its own.
constexpr std::chrono::milliseconds kCoalesceWindow {600};

bool isUtf8Continuation(unsigned char c) {
  return (c & 0xC0) == 0x80;
}

// Start offset of the UTF-8 codepoint immediately before `pos`.
std::size_t previousCodepoint(const std::string& text, std::size_t pos) {
  if(pos == 0) return 0;
  std::size_t i = pos - 1;
  while(i > 0 && isUtf8Continuation(static_cast<unsigned char>(text[i]))) --i;
  return i;
}

// Start offset of the UTF-8 codepoint immediately after `pos`.
std::size_t nextCodepoint(const std::string& text, std::size_t pos) {
  if(pos >= text.size()) return text.size();
  std::size_t i = pos + 1;
  while(i < text.size() && isUtf8Continuation(static_cast<unsigned char>(text[i]))) ++i;
  return i;
}

bool isSpaceByte(char c) {
  const auto value = static_cast<unsigned char>(c);
  return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

// Everything non-ASCII counts as a word byte: it keeps accented and CJK text in
// one word instead of breaking at every multi-byte codepoint.
bool isWordByte(char c) {
  const auto value = static_cast<unsigned char>(c);
  if(value >= 0x80) return true;
  return std::isalnum(value) != 0 || value == '_';
}

}

void MarkdownEditor::setText(std::string text) {
  text_ = std::move(text);
  cursor_ = text_.size();
  selectionAnchor_ = cursor_;
  selecting_ = false;
  dirty_ = false;
  undo_.clear();
  redo_.clear();
  breakUndoGroup();
}

void MarkdownEditor::insert(std::string_view text) {
  if(text.empty() && !hasSelection()) return;
  // Replacing a selection, or typing a line break, is a boundary worth undoing
  // on its own.
  const bool structural = hasSelection() || text.find('\n') != std::string_view::npos;
  snapshot(structural ? EditKind::Structural : EditKind::Insert);
  if(hasSelection()) {
    const auto start = selectionStart();
    text_.erase(start, selectionEnd() - start);
    cursor_ = start;
    clearSelection();
  }
  text_.insert(cursor_, text);
  cursor_ += text.size();
  clearSelection();
  dirty_ = true;
  closeEdit();
}

void MarkdownEditor::replaceRange(std::size_t start, std::size_t end, std::string_view text) {
  start = std::min(start, text_.size());
  end = std::clamp(end, start, text_.size());
  if(start == end && text.empty()) return;
  snapshot(EditKind::Structural);
  text_.erase(start, end - start);
  text_.insert(start, text);
  cursor_ = start + text.size();
  clearSelection();
  dirty_ = true;
  closeEdit();
}

void MarkdownEditor::erasePrevious() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  if(cursor_ == 0) return;
  snapshot(EditKind::Erase);
  const auto previous = previousCodepoint(text_, cursor_);
  text_.erase(previous, cursor_ - previous);
  cursor_ = previous;
  clearSelection();
  dirty_ = true;
  closeEdit();
}

void MarkdownEditor::eraseNext() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  if(cursor_ >= text_.size()) return;
  snapshot(EditKind::Erase);
  text_.erase(cursor_, nextCodepoint(text_, cursor_) - cursor_);
  clearSelection();
  dirty_ = true;
  closeEdit();
}

void MarkdownEditor::erasePreviousWord() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  const auto start = wordStartBefore(cursor_);
  if(start == cursor_) return;
  replaceRange(start, cursor_, "");
}

void MarkdownEditor::eraseNextWord() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  const auto end = wordEndAfter(cursor_);
  if(end == cursor_) return;
  replaceRange(cursor_, end, "");
}

void MarkdownEditor::moveCursor(std::size_t cursor) {
  cursor_ = std::min(cursor, text_.size());
  clearSelection();
  breakUndoGroup();
}

void MarkdownEditor::moveTo(std::size_t offset, bool keepSelection) {
  // Extending starts from the live selection's anchor, or from where the caret
  // stands when there is no selection to extend.
  const auto anchor = keepSelection ? (hasSelection() ? selectionAnchor_ : cursor_) : offset;
  cursor_ = std::min(offset, text_.size());
  if(keepSelection) selectRange(anchor, cursor_);
  else clearSelection();
  breakUndoGroup();
}

void MarkdownEditor::selectRange(std::size_t anchor, std::size_t cursor) {
  selectionAnchor_ = std::min(anchor, text_.size());
  cursor_ = std::min(cursor, text_.size());
  selecting_ = selectionAnchor_ != cursor_;
}

void MarkdownEditor::selectAll() {
  selectionAnchor_ = 0;
  cursor_ = text_.size();
  selecting_ = !text_.empty();
}

void MarkdownEditor::clearSelection() {
  selecting_ = false;
  selectionAnchor_ = cursor_;
}

bool MarkdownEditor::hasSelection() const {
  return selecting_ && selectionAnchor_ != cursor_;
}

std::size_t MarkdownEditor::selectionAnchor() const {
  return selectionAnchor_;
}

std::size_t MarkdownEditor::selectionStart() const {
  return std::min(selectionAnchor_, cursor_);
}

std::size_t MarkdownEditor::selectionEnd() const {
  return std::max(selectionAnchor_, cursor_);
}

std::string MarkdownEditor::selectedText() const {
  if(!hasSelection()) return "";
  return text_.substr(selectionStart(), selectionEnd() - selectionStart());
}

void MarkdownEditor::eraseSelection() {
  if(!hasSelection()) return;
  snapshot(EditKind::Structural);
  const auto start = selectionStart();
  text_.erase(start, selectionEnd() - start);
  cursor_ = start;
  clearSelection();
  dirty_ = true;
  closeEdit();
}

void MarkdownEditor::moveLeft(bool keepSelection) {
  // Collapsing a selection to its near edge is what every editor does.
  if(!keepSelection && hasSelection()) {
    cursor_ = selectionStart();
    clearSelection();
    return;
  }
  moveTo(previousCodepoint(text_, cursor_), keepSelection);
}

void MarkdownEditor::moveRight(bool keepSelection) {
  if(!keepSelection && hasSelection()) {
    cursor_ = selectionEnd();
    clearSelection();
    return;
  }
  moveTo(nextCodepoint(text_, cursor_), keepSelection);
}

std::size_t MarkdownEditor::wordStartBefore(std::size_t offset) const {
  std::size_t pos = std::min(offset, text_.size());
  while(pos > 0 && isSpaceByte(text_[pos - 1])) --pos;
  if(pos == 0) return 0;
  if(isWordByte(text_[pos - 1])) {
    while(pos > 0 && isWordByte(text_[pos - 1])) --pos;
  } else {
    while(pos > 0 && !isWordByte(text_[pos - 1]) && !isSpaceByte(text_[pos - 1])) --pos;
  }
  return pos;
}

std::size_t MarkdownEditor::wordEndAfter(std::size_t offset) const {
  std::size_t pos = std::min(offset, text_.size());
  while(pos < text_.size() && isSpaceByte(text_[pos])) ++pos;
  if(pos >= text_.size()) return text_.size();
  if(isWordByte(text_[pos])) {
    while(pos < text_.size() && isWordByte(text_[pos])) ++pos;
  } else {
    while(pos < text_.size() && !isWordByte(text_[pos]) && !isSpaceByte(text_[pos])) ++pos;
  }
  return pos;
}

void MarkdownEditor::moveWordLeft(bool keepSelection) {
  moveTo(wordStartBefore(cursor_), keepSelection);
}

void MarkdownEditor::moveWordRight(bool keepSelection) {
  moveTo(wordEndAfter(cursor_), keepSelection);
}

void MarkdownEditor::moveLineUp(bool keepSelection) {
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  if(lineStart == std::string::npos) {
    moveTo(0, keepSelection);
    return;
  }
  const auto previousEnd = lineStart;
  const auto previousStart = text_.rfind('\n', previousEnd == 0 ? 0 : previousEnd - 1);
  const auto column = cursor_ - lineStart - 1;
  const auto targetStart = previousStart == std::string::npos ? 0 : previousStart + 1;
  const auto targetLength = previousEnd - targetStart;
  moveTo(targetStart + std::min(column, targetLength), keepSelection);
}

void MarkdownEditor::moveLineDown(bool keepSelection) {
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  const auto column = cursor_ - (lineStart == std::string::npos ? 0 : lineStart + 1);
  const auto currentEnd = text_.find('\n', cursor_);
  if(currentEnd == std::string::npos) {
    moveTo(text_.size(), keepSelection);
    return;
  }
  const auto nextStart = currentEnd + 1;
  const auto nextEnd = text_.find('\n', nextStart);
  const auto targetEnd = nextEnd == std::string::npos ? text_.size() : nextEnd;
  moveTo(nextStart + std::min(column, targetEnd - nextStart), keepSelection);
}

void MarkdownEditor::moveLineStart(bool keepSelection) {
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  moveTo(lineStart == std::string::npos ? 0 : lineStart + 1, keepSelection);
}

void MarkdownEditor::moveLineEnd(bool keepSelection) {
  const auto lineEnd = text_.find('\n', cursor_);
  moveTo(lineEnd == std::string::npos ? text_.size() : lineEnd, keepSelection);
}

void MarkdownEditor::moveDocumentStart(bool keepSelection) {
  moveTo(0, keepSelection);
}

void MarkdownEditor::moveDocumentEnd(bool keepSelection) {
  moveTo(text_.size(), keepSelection);
}

bool MarkdownEditor::undo() {
  if(undo_.empty()) return false;
  redo_.push_back({text_, cursor_});
  text_ = std::move(undo_.back().text);
  cursor_ = std::min(undo_.back().cursor, text_.size());
  undo_.pop_back();
  clearSelection();
  dirty_ = true;
  breakUndoGroup();
  return true;
}

bool MarkdownEditor::redo() {
  if(redo_.empty()) return false;
  undo_.push_back({text_, cursor_});
  text_ = std::move(redo_.back().text);
  cursor_ = std::min(redo_.back().cursor, text_.size());
  redo_.pop_back();
  clearSelection();
  dirty_ = true;
  breakUndoGroup();
  return true;
}

void MarkdownEditor::breakUndoGroup() {
  groupOpen_ = false;
  groupKind_ = EditKind::Structural;
}

const std::string& MarkdownEditor::text() const {
  return text_;
}

std::size_t MarkdownEditor::cursor() const {
  return cursor_;
}

bool MarkdownEditor::dirty() const {
  return dirty_;
}

void MarkdownEditor::markDirty() {
  dirty_ = true;
}

void MarkdownEditor::markSaved() {
  dirty_ = false;
  breakUndoGroup();
}

void MarkdownEditor::snapshot(EditKind kind) {
  const auto now = std::chrono::steady_clock::now();
  const bool contiguous = groupOpen_ && kind == groupKind_ && cursor_ == groupEnd_ &&
                          now - groupAt_ <= kCoalesceWindow;
  if(kind != EditKind::Structural && contiguous) {
    // Fold into the open step: the pre-edit text is already on the stack.
    groupAt_ = now;
    redo_.clear();
    return;
  }
  if(undo_.empty() || undo_.back().text != text_) {
    undo_.push_back({text_, cursor_});
    if(undo_.size() > kMaxUndoSnapshots) undo_.erase(undo_.begin());
  }
  redo_.clear();
  groupOpen_ = kind != EditKind::Structural;
  groupKind_ = kind;
  groupAt_ = now;
}

void MarkdownEditor::closeEdit() {
  groupEnd_ = cursor_;
  groupAt_ = std::chrono::steady_clock::now();
}

}
