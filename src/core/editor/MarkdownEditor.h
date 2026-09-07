#pragma once

#include <chrono>
#include <cstdint>
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
  // Zero for `toRevision` means nothing has changed yet.
  struct TextChange {
    std::uint64_t fromRevision = 0;
    std::uint64_t toRevision = 0;
    std::size_t start = 0;
    std::size_t oldEnd = 0;
    std::size_t newEnd = 0;
  };
  const TextChange& lastChange() const;
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

  // Retained undo bytes and record count, exposed for tests and the harness.
  std::size_t undoBytes() const;
  // Whitespace-delimited words in the buffer. O(1): maintained by every edit.
  std::size_t wordCount() const;
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

  void snapshot(EditKind kind);
  // Brings the undo history back inside its count and byte ceilings.
  void trimUndo();
  void closeEdit();

  std::string text_;
  std::size_t cursor_ = 0;
  std::size_t selectionAnchor_ = 0;
  bool selecting_ = false;
  bool dirty_ = false;
  std::uint64_t revision_ = 0;
  TextChange lastChange_;
  // Whitespace-delimited words in `text_`, carried across every edit rather
  // than recounted. The status bar asks for this on every keystroke, and
  // walking a 200 KB note to answer took 247 us -- seventeen times the cost of
  // that keystroke's own layout update.
  std::size_t words_ = 0;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  EditKind groupKind_ = EditKind::Structural;
  bool groupOpen_ = false;
  std::size_t groupEnd_ = 0;
  std::chrono::steady_clock::time_point groupAt_ {};
};

}
