#pragma once

#include "core/editor/TextEdit.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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
//
//  * A step then stayed a copy of the whole document for a while longer, which
//    made the history cost the note's size times its depth: a long session on a
//    *2 KB* note retained 214.6 KB, a hundred and seven times the note it
//    belonged to, and Ctrl+Z charged a 200 KB copy and a full word recount.
//    A step is the splice that reverses the edit now -- an offset, the bytes
//    that came out, and the length of the bytes that went in -- so the history
//    costs what was *edited* rather than what was open, and undo is an edit
//    like any other rather than a whole-buffer replacement.
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

  // The change that took the buffer from one revision to the next.
  //
  // Everything outside [start, oldEnd) of the buffer at `fromRevision`, and
  // outside [start, newEnd) of the buffer at `toRevision`, is byte-for-byte
  // unchanged. That is what a consumer holding the older buffer needs in order
  // to bound work that is otherwise a pass over the whole note: the live
  // layout's edit window is two `memcmp` passes summing to the length of the
  // note, run to locate one typed character.
  //
  // Both revisions are here so a consumer can *check* rather than trust. One
  // standing on some other revision -- two edits landed between its frames, or
  // it skipped one -- sees a pair that does not match what it holds and falls
  // back to comparing bytes. A whole-buffer replacement (setText, undo, redo)
  // honestly reports the whole buffer, which bounds nothing and so needs no
  // special case at either end.
  //
  // See `editor::TextEdit`, which is the type: it was declared here and again
  // as `editor::TextEdit`, five fields at a time, with the
  // contract written out in both places.
  const TextEdit& lastChange() const;
  // Ends the open typing run, so the next edit starts a fresh undo step.
  // Called on focus changes, saves, and anything structural.
  void breakUndoGroup();
  const std::string& text() const;
  std::size_t cursor() const;
  bool dirty() const;
  // Bumped by every call that changes the text, and by nothing else.
  //
  // Consumers derive things from the buffer per frame -- a word count, a block
  // scan -- and a frame happens for reasons that have nothing to do with the
  // text: a scroll, a hover, a window focus. Without a cheap "is this still the
  // same buffer" answer they either recompute every frame or compare the whole
  // string, and the first is what the status bar was doing.
  //
  // It is a revision, not a hash: undo to an identical earlier state is a new
  // revision. That direction is the safe one -- a consumer recomputes something
  // it did not have to, rather than showing a stale answer.
  std::uint64_t revision() const;
  void markDirty();
  void markSaved();

  // Bytes the undo and redo histories retain, and the number of steps in the
  // undo one. Exposed for tests and for the harness's undo lane, which gates on
  // the first against the size of the note it belongs to.
  std::size_t undoBytes() const;
  // Whitespace-delimited words in the buffer. O(1): maintained by every edit.
  std::size_t wordCount() const;
  std::size_t undoDepth() const;

private:
  // Consecutive edits of the same kind at the same spot fold into one undo
  // step; anything else opens a new one.
  enum class EditKind { Structural, Insert, Erase };

  // One undo step: the splice that puts the buffer back. Applying it means
  // "restore `removed` at `start`, taking out the `insertedLen` bytes standing
  // there now" -- which is a complete description of the reverse of any edit,
  // because every edit here is one splice.
  //
  // It is also its own inverse's shape: applying a record yields another record
  // that undoes it, so one type and one function serve both stacks.
  //
  // The selection rides along so Ctrl+Z puts the caret and the highlight back
  // where they were, not just the characters. These are the values from
  // *before* the edit, captured when the step opened, so a coalesced run of
  // typing rewinds to where the run started.
  struct EditRecord {
    std::string removed;
    std::size_t start = 0;
    std::size_t insertedLen = 0;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    bool selecting = false;
  };

  // Every text mutation goes through here, so `dirty_`, `revision_` and
  // `lastChange_` cannot drift apart: a site that forgets to mark the buffer
  // changed also fails to save it, which is a bug nobody ships. It takes the
  // span it changed rather than deriving it, so a new mutation cannot be added
  // without saying what it touched -- `lastChange` is only sound while that
  // holds, and a compile error is how it keeps holding.
  void markChanged(std::size_t start, std::size_t oldEnd, std::size_t newEnd);
  // The span half of the above, for the one site that bumps `revision_` itself
  // because it is *not* an edit: `setText` replaces the buffer and clears the
  // dirty flag rather than setting it.
  void recordChange(std::size_t start, std::size_t oldEnd, std::size_t newEnd);

  // The one place the buffer's bytes change: applies the splice, and carries
  // the word count across it without rereading the note. See the .cpp for why
  // the delta is exact rather than an estimate.
  void splice(std::size_t start, std::size_t oldEnd, std::string_view text);
  // Word starts in `[from, to)` -- a non-space byte whose predecessor is a
  // space, or byte zero. The count is the number of these in the buffer.
  std::size_t wordStartsIn(std::size_t from, std::size_t to) const;
  // For the three mutations that replace the whole buffer and so have nothing
  // to carry: `setText`, undo and redo.
  void recountWords();

  // The single path every text mutation takes. It records the step, applies the
  // splice, puts the caret at the end of what went in and reports the span --
  // in that order, because the record needs the bytes the splice is about to
  // overwrite. The five editing entry points used to do those things each in
  // their own order, which is five chances to get one of them wrong.
  //
  // The caret lands at `start + text.size()` for all five: an insertion ends
  // after what it inserted, and an erase inserts nothing, so that is `start`.
  void applyEdit(EditKind kind, std::size_t start, std::size_t oldEnd, std::string_view text);
  // Opens a new undo step for this edit, or folds it into the open one.
  void recordEdit(EditKind kind, std::size_t start, std::size_t oldEnd, std::string_view text);
  // Folds a contiguous edit into the step already on the stack. False when the
  // two cannot be described as one splice, which opens a fresh step instead.
  bool extendOpenStep(std::size_t start, std::size_t oldEnd, std::string_view text);
  // Applies a record's splice and returns the record that undoes it, so undo
  // and redo are the same operation reading from different stacks.
  EditRecord applyRecord(const EditRecord& record);
  // Pushes a step and brings the history back inside its ceilings.
  void pushStep(EditRecord record);
  // Brings the undo history back inside its count and byte ceilings.
  void trimUndo();
  // What one step costs the process, by the same measure `undoBytes` reports
  // and `trimUndo` enforces -- so the ceiling is expressed in the thing it
  // actually bounds.
  static std::size_t stepBytes(const EditRecord& record);

  std::string text_;
  std::size_t cursor_ = 0;
  std::size_t selectionAnchor_ = 0;
  bool selecting_ = false;
  bool dirty_ = false;
  std::uint64_t revision_ = 0;
  TextEdit lastChange_;
  // Whitespace-delimited words in `text_`, carried across every edit rather
  // than recounted. The status bar asks for this on every keystroke, and
  // walking a 200 KB note to answer took 247 us -- seventeen times the cost of
  // that keystroke's own layout update.
  std::size_t words_ = 0;
  std::vector<EditRecord> undo_;
  std::vector<EditRecord> redo_;
  // Running totals, so `undoBytes` is O(1) and `trimUndo` costs what it drops
  // rather than re-summing the whole history on every keystroke.
  std::size_t undoBytes_ = 0;
  std::size_t redoBytes_ = 0;
  EditKind groupKind_ = EditKind::Structural;
  bool groupOpen_ = false;
  std::size_t groupEnd_ = 0;
  std::chrono::steady_clock::time_point groupAt_ {};
};

}
