#include "CoreAliases.h"
#include "ui/OpenNoteRecord.h"

#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/platform/DurableFile.h"

#include <utility>

namespace micronotes::ui {

const OpenNote& OpenNoteRecord::load(std::string* body) const {
  static const OpenNote kNone;
  if(!catalog_.isOpen() || noteId_.empty()) return kNone;
  // A hit only when the memo still names the note it was read for. The library
  // revision deliberately does not come into it: a save bumps the revision, and
  // re-reading the note micronotes has just written is exactly the read this
  // exists to avoid.
  if(note_ && note_->noteId == noteId_ && !body) return *note_;
  const library::NoteListItem* item = catalog_.noteById(noteId_);
  if(!item) return kNone;
  auto loaded = catalog_.loadNote(item->path);
  // Moved, not copied: `loaded.body` is a local that is discarded on the next
  // line but one, so the copy was a whole note per read for nobody.
  if(body) *body = std::move(loaded.body);
  OpenNote open;
  open.noteId = noteId_;
  open.path = item->path;
  open.metadata = std::move(loaded.metadata);
  // Sampled after the read, not before. Between the two the file could change,
  // and a signature from before the read would claim agreement with bytes that
  // are no longer there -- which is the one direction that loses data.
  open.disk = platform::statFile(item->path);
  note_ = std::move(open);
  perf::addCounter(perf::CounterId::AppStateOpenNoteReads);
  return *note_;
}

const OpenNote& OpenNoteRecord::get() const {
  return load(nullptr);
}

std::optional<LoadedNote> OpenNoteRecord::read() const {
  std::string body;
  const OpenNote& open = load(&body);
  if(open.noteId.empty()) return std::nullopt;
  const library::NoteListItem* item = catalog_.noteById(open.noteId);
  if(!item) return std::nullopt;
  return LoadedNote {*item, open.metadata, std::move(body)};
}

DiskState OpenNoteRecord::diskState() const {
  const OpenNote& open = get();
  // Nothing open, or a note whose file was already absent when it was read:
  // there is no baseline to disagree with.
  if(open.noteId.empty() || !open.disk.exists) return DiskState::Agrees;
  const auto now = platform::statFile(open.path);
  if(now.error) return DiskState::Unknown;
  if(!now.exists) return DiskState::Vanished;
  return now.sameContentAs(open.disk) ? DiskState::Agrees : DiskState::Changed;
}

void OpenNoteRecord::forget() {
  note_.reset();
}

void OpenNoteRecord::recordWrite(const std::filesystem::path& path,
                                 library::NoteMetadata metadata) {
  if(!note_) return;
  note_->path = path;
  note_->metadata = std::move(metadata);
  note_->disk = platform::statFile(path);
}

void OpenNoteRecord::adoptId(std::string noteId) {
  if(!note_) return;
  note_->noteId = std::move(noteId);
}

void OpenNoteRecord::setRecoveryRoot(const std::filesystem::path& libraryRoot) {
  recovery_.setRoot(libraryRoot);
}

bool OpenNoteRecord::saveRecovery(std::string_view body) const {
  if(!catalog_.isOpen() || noteId_.empty()) return false;
  return recovery_.save(noteId_, body);
}

bool OpenNoteRecord::clearRecovery() const {
  if(!catalog_.isOpen() || noteId_.empty()) return true;
  return recovery_.clear(noteId_);
}

std::optional<std::string> OpenNoteRecord::recoveryBody() const {
  if(!catalog_.isOpen() || noteId_.empty()) return std::nullopt;
  return recovery_.read(noteId_);
}

}
