#include "core/editor/MarkdownEditor.h"

#include "core/perf/PerformanceCounters.h"
#include "core/util/Utf8.h"

#include <algorithm>
#include <cctype>

namespace microcore::editor {
namespace {

// Two ceilings, and which of the two does the work changed completely when a
// step stopped being a copy of the buffer.
//
// While it was, the count was the only policy and could not bound the thing
// that mattered: 100 whole-buffer snapshots of a 200 KB note is 19.6 MB of undo
// for one open note, against a whole-process peak RSS of about 30 MB on the
// perf fixture, and a long session on a *2 KB* note still retained 214.6 KB --
// a hundred and seven times the note. The test meant to guard it asserted a
// byte figure but drove a 4 KB buffer, so it measured 400 KB and passed easily.
//
// A step is a splice now, so it costs a few dozen bytes plus whatever text the
// edit *deleted*. That makes the count the ordinary ceiling -- 100 steps of
// typing is about 7 KB whatever the note's size -- and leaves the byte budget
// for the one case that genuinely retains bytes: deleting a lot of text, where
// the history holds the only remaining copy of what came out. A hundred steps
// is a policy choice about how far back Ctrl+Z should reach, not a concession
// to memory, which is what it used to be.
constexpr std::size_t kMaxUndoSteps = 100;
constexpr std::size_t kMaxUndoBytes = 8u * 1024u * 1024u;
// However big the deletions, this many steps survive the byte budget. An editor
// that can undo once is not an editor with undo, and one paste over a whole
// 12 MB note must not be the thing that empties the history.
constexpr std::size_t kMinUndoSteps = 8;
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

// Vertical motion keeps the column in code points, not bytes. On a line holding
// multi-byte characters a byte column lands at a different character on the
// line below -- or mid-character, which is worse.
std::size_t codepointColumn(const std::string& text, std::size_t lineStart, std::size_t pos) {
  std::size_t column = 0;
  for(std::size_t i = lineStart; i < pos && i < text.size(); ++i) {
    if(!isUtf8Continuation(static_cast<unsigned char>(text[i]))) ++column;
  }
  return column;
}

std::size_t offsetForColumn(const std::string& text, std::size_t lineStart, std::size_t lineEnd, std::size_t column) {
  std::size_t i = lineStart;
  while(column > 0 && i < lineEnd) {
    i = nextCodepoint(text, i);
    --column;
  }
  return i < lineEnd ? i : lineEnd;
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

// Word starts in `[from, to)`: positions holding a non-space byte whose
// predecessor is a space, or the very first byte. The buffer's word count is
// the number of these in the whole of it, which is what makes the count a sum
// over positions and therefore something an edit can adjust rather than redo.
std::size_t MarkdownEditor::wordStartsIn(std::size_t from, std::size_t to) const {
  std::size_t words = 0;
  for(std::size_t i = from; i < to; ++i) {
    if(isSpaceByte(text_[i])) continue;
    if(i == 0 || isSpaceByte(text_[i - 1])) ++words;
  }
  return words;
}

// The one place the buffer's bytes change.
//
// Five call sites used to do their own `erase`/`insert` pair and then tell
// `markChanged` separately what they had done -- the same fact stated twice,
// with nothing checking the two agreed. Here the span reported is the span
// applied, by construction.
//
// It is also the only place a running word count can be kept, because it is the
// only place holding both the bytes going out and the bytes coming in. That
// matters because the status bar's count is asked for on every keystroke, and
// counting a 200 KB note takes 247 us -- seventeen times what the keystroke's
// own layout update costs, and the largest single thing a keystroke did.
//
// The window is the replaced span **plus one byte**, and that is the exact
// extent of what an edit can change, not an approximation:
//
//   * a position below `start` is decided by two bytes both below `start`, and
//     neither moved;
//   * the position at the far end of the splice -- `oldEnd` before, `newEnd`
//     after -- holds an unchanged byte but its *predecessor* is the last byte of
//     the replaced span, so its status can flip. That is the `+ 1`;
//   * everything past that is decided by two bytes of the untouched suffix,
//     which sit at the same distance from the end on both sides.
//
// An earlier version widened the window to the nearest whitespace on each side,
// which is also correct and is O(document) on a buffer that has no whitespace in
// it -- a minified file, a base64 blob, `std::string(12 MB, 'x')`. That is the
// exact shape this exists to avoid, and the undo tests caught it.
void MarkdownEditor::splice(std::size_t start, std::size_t oldEnd, std::string_view text) {
  const std::size_t before = wordStartsIn(start, std::min(oldEnd + 1, text_.size()));

  text_.replace(start, oldEnd - start, text);

  const std::size_t newEnd = start + text.size();
  const std::size_t to = std::min(newEnd + 1, text_.size());
  perf::addCounter(perf::CounterId::EditorWordCountUpdates);
  perf::addCounter(perf::CounterId::EditorWordCountBytesScanned,
                   (std::min(oldEnd + 1, text_.size()) - start) + (to - start));
  words_ = words_ + wordStartsIn(start, to) - before;
}

// Every byte is new, so there is nothing to carry. Only `setText` now: undo and
// redo go through `splice` like any other edit and carry the count across.
void MarkdownEditor::recountWords() {
  perf::addCounter(perf::CounterId::EditorWordCountRebuilds);
  perf::addCounter(perf::CounterId::EditorWordCountBytesScanned, text_.size());
  words_ = wordStartsIn(0, text_.size());
}

std::size_t MarkdownEditor::wordCount() const {
  return words_;
}

void MarkdownEditor::setText(std::string text) {
  const std::size_t was = text_.size();
  text_ = std::move(text);
  recountWords();
  cursor_ = text_.size();
  selectionAnchor_ = cursor_;
  selecting_ = false;
  dirty_ = false;
  // The whole buffer, honestly: nothing about the old one survives.
  recordChange(0, was, text_.size());
  ++revision_;
  undo_.clear();
  redo_.clear();
  undoBytes_ = 0;
  redoBytes_ = 0;
  breakUndoGroup();
}

void MarkdownEditor::insert(std::string_view text) {
  if(text.empty() && !hasSelection()) return;
  perf::addCounter(perf::CounterId::EditorInsertCalls);
  // Replacing a selection, or typing a line break, is a boundary worth undoing
  // on its own.
  const bool structural = hasSelection() || text.find('\n') != std::string_view::npos;
  const std::size_t from = hasSelection() ? selectionStart() : cursor_;
  const std::size_t replaced = hasSelection() ? selectionEnd() : cursor_;
  applyEdit(structural ? EditKind::Structural : EditKind::Insert, from, replaced, text);
}

void MarkdownEditor::replaceRange(std::size_t start, std::size_t end, std::string_view text) {
  start = std::min(start, text_.size());
  end = std::clamp(end, start, text_.size());
  if(start == end && text.empty()) return;
  perf::addCounter(perf::CounterId::EditorEraseCalls);
  applyEdit(EditKind::Structural, start, end, text);
}

void MarkdownEditor::erasePrevious() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  if(cursor_ == 0) return;
  applyEdit(EditKind::Erase, previousCodepoint(text_, cursor_), cursor_, {});
}

void MarkdownEditor::eraseNext() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  if(cursor_ >= text_.size()) return;
  applyEdit(EditKind::Erase, cursor_, nextCodepoint(text_, cursor_), {});
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
  breakUndoGroup();
}

void MarkdownEditor::selectAll() {
  selectionAnchor_ = 0;
  cursor_ = text_.size();
  selecting_ = !text_.empty();
  breakUndoGroup();
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
  applyEdit(EditKind::Structural, selectionStart(), selectionEnd(), {});
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
  const auto column = codepointColumn(text_, lineStart + 1, cursor_);
  const auto targetStart = previousStart == std::string::npos ? 0 : previousStart + 1;
  moveTo(offsetForColumn(text_, targetStart, previousEnd, column), keepSelection);
}

void MarkdownEditor::moveLineDown(bool keepSelection) {
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  const auto column = codepointColumn(text_, lineStart == std::string::npos ? 0 : lineStart + 1, cursor_);
  const auto currentEnd = text_.find('\n', cursor_);
  if(currentEnd == std::string::npos) {
    moveTo(text_.size(), keepSelection);
    return;
  }
  const auto nextStart = currentEnd + 1;
  const auto nextEnd = text_.find('\n', nextStart);
  const auto targetEnd = nextEnd == std::string::npos ? text_.size() : nextEnd;
  moveTo(offsetForColumn(text_, nextStart, targetEnd, column), keepSelection);
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

std::size_t MarkdownEditor::undoDepth() const {
  return undo_.size();
}

// What a step costs the process. `capacity` rather than `size` so the figure
// never understates the ceiling it is used to enforce; for a short string the
// capacity is already inside `sizeof(EditRecord)`, which over-counts by a few
// bytes a step, and that is the direction a ceiling should err in.
//
// The vectors' own capacity is deliberately not in here: it is one allocation
// bounded by `kMaxUndoSteps` entries, and folding it in would make the figure
// jump on a `push_back` that retained nothing.
std::size_t MarkdownEditor::stepBytes(const EditRecord& record) {
  return sizeof(EditRecord) + record.removed.capacity();
}

std::size_t MarkdownEditor::undoBytes() const {
  return undoBytes_ + redoBytes_;
}

// Applies a record's splice and hands back the record that reverses it.
//
// The inverse is built *before* the splice, because the bytes it has to keep in
// order to redo -- the ones the record is about to displace -- are sitting in
// the buffer right now and nowhere else. That symmetry is the whole reason one
// function serves both directions: undo pops from `undo_` and pushes the
// inverse to `redo_`, redo does the same the other way round, and neither knows
// which it is.
//
// Going through `splice` rather than replacing the buffer is what makes undo an
// ordinary edit: the word count is carried across it instead of recounted, and
// `markChanged` reports the span that actually moved instead of the whole note.
// Ctrl+Z on a 200 KB note used to charge a full buffer copy onto the redo stack
// plus a 200 KB word recount, and then tell the layout every byte had changed.
MarkdownEditor::EditRecord MarkdownEditor::applyRecord(const EditRecord& record) {
  const std::size_t start = std::min(record.start, text_.size());
  const std::size_t oldEnd = std::min(start + record.insertedLen, text_.size());

  EditRecord inverse;
  inverse.start = start;
  inverse.removed.assign(text_, start, oldEnd - start);
  inverse.insertedLen = record.removed.size();
  inverse.cursor = cursor_;
  inverse.anchor = selectionAnchor_;
  inverse.selecting = selecting_;

  splice(start, oldEnd, record.removed);
  cursor_ = std::min(record.cursor, text_.size());
  if(record.selecting) {
    selectionAnchor_ = std::min(record.anchor, text_.size());
    selecting_ = true;
  } else {
    clearSelection();
  }
  markChanged(start, oldEnd, start + record.removed.size());
  return inverse;
}

bool MarkdownEditor::undo() {
  if(undo_.empty()) return false;
  undoBytes_ -= stepBytes(undo_.back());
  const EditRecord record = std::move(undo_.back());
  undo_.pop_back();
  EditRecord inverse = applyRecord(record);
  redoBytes_ += stepBytes(inverse);
  redo_.push_back(std::move(inverse));
  breakUndoGroup();
  return true;
}

bool MarkdownEditor::redo() {
  if(redo_.empty()) return false;
  redoBytes_ -= stepBytes(redo_.back());
  const EditRecord record = std::move(redo_.back());
  redo_.pop_back();
  EditRecord inverse = applyRecord(record);
  undoBytes_ += stepBytes(inverse);
  undo_.push_back(std::move(inverse));
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
  // Nothing moved, so every span is an honest one; the whole buffer is the one
  // that bounds nothing, which is the right answer for a caller that is saying
  // "assume this changed" rather than saying what did.
  markChanged(0, text_.size(), text_.size());
}

void MarkdownEditor::recordChange(std::size_t start, std::size_t oldEnd, std::size_t newEnd) {
  lastChange_ = TextChange {revision_, revision_ + 1, start, oldEnd, newEnd};
}

void MarkdownEditor::markChanged(std::size_t start, std::size_t oldEnd, std::size_t newEnd) {
  recordChange(start, oldEnd, newEnd);
  dirty_ = true;
  ++revision_;
}

const MarkdownEditor::TextChange& MarkdownEditor::lastChange() const {
  return lastChange_;
}

std::uint64_t MarkdownEditor::revision() const {
  return revision_;
}

void MarkdownEditor::markSaved() {
  dirty_ = false;
  breakUndoGroup();
}

void MarkdownEditor::applyEdit(EditKind kind, std::size_t start, std::size_t oldEnd,
                               std::string_view text) {
  recordEdit(kind, start, oldEnd, text);
  splice(start, oldEnd, text);
  cursor_ = start + text.size();
  clearSelection();
  markChanged(start, oldEnd, cursor_);
  groupEnd_ = cursor_;
  groupAt_ = std::chrono::steady_clock::now();
}

void MarkdownEditor::recordEdit(EditKind kind, std::size_t start, std::size_t oldEnd,
                                std::string_view text) {
  const auto now = std::chrono::steady_clock::now();
  const bool contiguous = groupOpen_ && kind == groupKind_ && cursor_ == groupEnd_ &&
                          now - groupAt_ <= kCoalesceWindow;
  // Editing forwards discards the redo branch, whether or not this edit opens a
  // new step.
  redo_.clear();
  redoBytes_ = 0;
  if(kind != EditKind::Structural && contiguous && extendOpenStep(start, oldEnd, text)) {
    perf::addCounter(perf::CounterId::EditorUndoRecordsCoalesced);
    groupAt_ = now;
    return;
  }

  EditRecord record;
  record.start = start;
  record.removed.assign(text_, start, oldEnd - start);
  record.insertedLen = text.size();
  record.cursor = cursor_;
  record.anchor = selectionAnchor_;
  record.selecting = selecting_;
  pushStep(std::move(record));

  groupOpen_ = kind != EditKind::Structural;
  groupKind_ = kind;
  groupAt_ = now;
}

// A snapshot history got coalescing for free: the pre-edit buffer was already on
// the stack, so folding a keystroke into the open step meant declining to push
// anything. A splice has to actually be widened, and the two directions a run
// can grow are not the same shape:
//
//   * typing extends the run's tail -- `insertedLen` grows and there is nothing
//     deleted to keep;
//   * Backspace walks left, each key taking the bytes immediately *before* the
//     ones already taken, so the step's start moves back and the new bytes go on
//     the front of what it has to put back;
//   * Delete eats forwards from a caret that stands still, so those bytes go on
//     the *end* of the same string.
//
// Each branch also re-checks the shape of the step it is folding into rather
// than trusting the group bookkeeping to have kept it: an Insert run's steps
// delete nothing and an Erase run's insert nothing, and if that ever stops
// holding the answer is a fresh step, not a flattened one. Returning false is
// always correct -- it costs one extra Ctrl+Z, where guessing wrong corrupts the
// buffer the user gets back.
bool MarkdownEditor::extendOpenStep(std::size_t start, std::size_t oldEnd, std::string_view text) {
  if(undo_.empty()) return false;
  EditRecord& step = undo_.back();
  const std::size_t was = stepBytes(step);
  const std::size_t insertedEnd = step.start + step.insertedLen;
  bool folded = false;
  if(!text.empty() && start == oldEnd && start == insertedEnd && step.removed.empty()) {
    step.insertedLen += text.size();
    folded = true;
  } else if(text.empty() && step.insertedLen == 0 && oldEnd == step.start) {
    step.removed.insert(0, text_, start, oldEnd - start);
    step.start = start;
    folded = true;
  } else if(text.empty() && step.insertedLen == 0 && start == step.start) {
    step.removed.append(text_, start, oldEnd - start);
    folded = true;
  }
  if(!folded) return false;
  undoBytes_ = undoBytes_ - was + stepBytes(step);
  perf::addCounter(perf::CounterId::EditorUndoBytesRetained, oldEnd - start);
  return true;
}

void MarkdownEditor::pushStep(EditRecord record) {
  perf::addCounter(perf::CounterId::EditorUndoRecords);
  perf::addCounter(perf::CounterId::EditorUndoBytesRetained, record.removed.size());
  undoBytes_ += stepBytes(record);
  undo_.push_back(std::move(record));
  trimUndo();
}

// Drops the oldest steps until the history is inside both ceilings, in one
// erase rather than one per step: removing from the front of a vector shifts
// everything above it, and one edit that deletes a lot of text can push several
// out at once.
//
// The byte figure is the running total rather than a fresh sum over the stack.
// Summing it here was O(depth) on every keystroke to answer a question that is
// almost always "no" -- and on the path where the answer is no, a hundred
// `size()` reads over a vector nobody is about to touch.
void MarkdownEditor::trimUndo() {
  std::size_t drop = undo_.size() > kMaxUndoSteps ? undo_.size() - kMaxUndoSteps : 0;
  std::size_t bytes = undoBytes_;
  for(std::size_t i = 0; i < drop; ++i) bytes -= stepBytes(undo_[i]);
  // Newest first, keeping what fits: the oldest steps are the ones nobody
  // reaches for, and they are what the budget spends its bytes on.
  while(bytes + redoBytes_ > kMaxUndoBytes && undo_.size() - drop > kMinUndoSteps) {
    bytes -= stepBytes(undo_[drop]);
    ++drop;
  }
  if(drop == 0) return;
  perf::addCounter(perf::CounterId::EditorUndoRecordsDropped, drop);
  undoBytes_ = bytes;
  undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(drop));
}

}
