#include "core/editor/SingleLineEditor.h"

namespace microcore::editor {

namespace {

bool hasLineBreak(std::string_view value) {
  return value.find_first_of("\r\n") != std::string_view::npos;
}

}

std::string flattenToSingleLine(std::string_view value) {
  std::string flat(value);
  for(auto& c : flat) {
    if(c == '\n' || c == '\r') c = ' ';
  }
  return flat;
}

SingleLineEditor::SingleLineEditor(std::string value) {
  setText(std::move(value));
}

void SingleLineEditor::setText(std::string value) {
  // Only pay for the copy when there is something to strip. Typing goes through
  // insert() one code point at a time, and a per-keystroke allocation on a path
  // that runs for every character is exactly the kind of cost that is invisible
  // until it is measured.
  if(hasLineBreak(value)) value = flattenToSingleLine(value);
  editor_.setText(std::move(value));
}

void SingleLineEditor::clear() {
  setText(std::string());
}

void SingleLineEditor::insert(std::string_view value) {
  if(!hasLineBreak(value)) {
    editor_.insert(value);
    return;
  }
  editor_.insert(flattenToSingleLine(value));
}

void SingleLineEditor::erasePrevious() {
  editor_.erasePrevious();
}

void SingleLineEditor::eraseNext() {
  editor_.eraseNext();
}

void SingleLineEditor::eraseSelection() {
  editor_.eraseSelection();
}

void SingleLineEditor::eraseWordBefore() {
  editor_.erasePreviousWord();
}

void SingleLineEditor::eraseWordAfter() {
  editor_.eraseNextWord();
}

void SingleLineEditor::moveCursor(std::size_t cursor) {
  editor_.moveCursor(cursor);
}

void SingleLineEditor::moveLeft(bool keepSelection) {
  editor_.moveLeft(keepSelection);
}

void SingleLineEditor::moveRight(bool keepSelection) {
  editor_.moveRight(keepSelection);
}

void SingleLineEditor::moveWordLeft(bool keepSelection) {
  editor_.moveWordLeft(keepSelection);
}

void SingleLineEditor::moveWordRight(bool keepSelection) {
  editor_.moveWordRight(keepSelection);
}

void SingleLineEditor::moveHome(bool keepSelection) {
  editor_.moveLineStart(keepSelection);
}

void SingleLineEditor::moveEnd(bool keepSelection) {
  editor_.moveLineEnd(keepSelection);
}

void SingleLineEditor::selectRange(std::size_t anchor, std::size_t cursor) {
  editor_.selectRange(anchor, cursor);
}

void SingleLineEditor::selectAll() {
  editor_.selectAll();
}

void SingleLineEditor::clearSelection() {
  editor_.clearSelection();
}

bool SingleLineEditor::hasSelection() const {
  return editor_.hasSelection();
}

std::size_t SingleLineEditor::selectionStart() const {
  return editor_.selectionStart();
}

std::size_t SingleLineEditor::selectionEnd() const {
  return editor_.selectionEnd();
}

std::string SingleLineEditor::selectedText() const {
  return editor_.selectedText();
}

bool SingleLineEditor::undo() {
  return editor_.undo();
}

bool SingleLineEditor::redo() {
  return editor_.redo();
}

const std::string& SingleLineEditor::text() const {
  return editor_.text();
}

std::size_t SingleLineEditor::cursor() const {
  return editor_.cursor();
}

bool SingleLineEditor::empty() const {
  return editor_.text().empty();
}

}
