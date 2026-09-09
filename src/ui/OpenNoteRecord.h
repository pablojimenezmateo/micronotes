#pragma once

#include "CoreAliases.h"

#include "core/platform/DurableFile.h"

#include "library/Metadata.h"
#include "library/NoteCatalog.h"
#include "library/Organization.h"
#include "library/RecoveryStore.h"

#include <filesystem>
#include <optional>
#include <string>

namespace micronotes::ui {

struct LoadedNote {
  library::NoteListItem item;
  library::NoteMetadata metadata;
  std::string body;
};

// The note the editor has open, as it was last read from or written to disk.
//
// Every question about the open note that is not about its *body* is answered
// from here instead of by opening the file again. It used to be a full read and
// a front-matter parse per asker per library revision, and the library revision
// moved on every save: the page header, the right-hand panel and the save
// itself each re-read the whole note once a second while somebody was typing
// into it.
//
// `disk` is what makes the save path safe. It is the identity of the file the
// front matter and the body were read from, so a stat before writing says
// whether anything else has touched the note in the meantime -- which is the
// difference between overwriting a `git checkout` and noticing it.
struct OpenNote {
  std::string noteId;   // empty when nothing is open
  std::filesystem::path path;
  library::NoteMetadata metadata;
  platform::FileSignature disk;
};

// Why the editor's copy of a note is out of step with the file.
enum class DiskState {
  Agrees,     // the file is as micronotes last read or wrote it
  Changed,    // something else has rewritten it
  Vanished,   // it was there and is not any more
  Unknown     // the stat failed; treated as Changed everywhere it matters
};

// The memo of the open note, and the draft of it that survives a crash.
//
// It reads the catalog and the selected id rather than being told them, because
// it is a memo *of* whatever is selected: a record that had to be pushed at
// would be a fourth thing to keep in step. Both are bound at construction and
// both outlive it -- they are siblings inside `AppState`.
class OpenNoteRecord {
public:
  OpenNoteRecord(const library::NoteCatalog& catalog, const std::string& noteId)
      : catalog_(catalog), noteId_(noteId) {}

  // The open note's front matter and the identity of its file. Opens the file
  // at most once per note rather than once per caller.
  const OpenNote& get() const;
  // The three parts of it that are asked for on their own, so that a caller
  // wanting one of them does not have to name the record twice.
  const std::string& noteId() const { return get().noteId; }
  const std::filesystem::path& path() const { return get().path; }
  const library::NoteMetadata& metadata() const { return get().metadata; }
  // The same, plus the note's *body* off the disk. The one reader of a note's
  // text: anything that wants a title, a tag or an icon wants `get()` and costs
  // nothing.
  std::optional<LoadedNote> read() const;
  // Whether the file still says what micronotes last read or wrote. One stat.
  DiskState diskState() const;

  // Forgets everything read, so the next question goes back to the disk.
  void forget();
  // Records what a write left behind: the front matter that went into the file
  // and, when the write moved it, where it landed. The record is the truth
  // about a file micronotes has just written, so nothing has to read it back.
  void recordWrite(const std::filesystem::path& path, library::NoteMetadata metadata);
  // The note is the same note under a new id -- a first save adopting a
  // permanent one, or a rename that generated one.
  void adoptId(std::string noteId);

  // --- the recovery copy ---------------------------------------------------

  void setRecoveryRoot(const std::filesystem::path& libraryRoot);
  // Queues the recovery copy. False when a write posted earlier has since
  // failed; see `library::RecoveryStore` for why the answer is about an earlier
  // post rather than this one.
  bool saveRecovery(std::string_view body) const;
  bool clearRecovery() const;
  std::optional<std::string> recoveryBody() const;

private:
  // Fills the memo for whatever the selection names, reading the file when the
  // selection has moved to a note this has not seen. `body` takes the text when
  // the caller wants it, so opening a note is one read rather than one for the
  // body and another for the front matter.
  const OpenNote& load(std::string* body) const;

  const library::NoteCatalog& catalog_;
  const std::string& noteId_;
  // Mutable because reading it is memoisation rather than a change of state:
  // every asker holds a const `AppState`.
  mutable std::optional<OpenNote> note_;
  // Mutable for the same reason: posting recovery is a side effect of editing,
  // not of changing the state, and every caller of it holds a const `AppState`.
  mutable library::RecoveryStore recovery_;
};

}
