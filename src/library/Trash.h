#pragma once

#include "CoreAliases.h"

#include "core/platform/PathUtils.h"

#include <filesystem>
#include <string>
#include <vector>

// The library's trash: a directory of filed files and one index line per entry
// saying where each came from.
//
// It lives inside the library rather than in the desktop's, because restoring
// has to work from inside the app and no system trash can be read back
// portably. It sits under the state directory, which `Library::walk` prunes, so
// a deleted note leaves the library the moment it is filed.
//
// This is a unit of its own because it is a *file format* with an ordering
// rule, and it was eight file-static helpers and four methods spread through
// `Library.cpp` -- a class that also reads notes, writes notes, walks the tree,
// renames folders and lists companion files. Two things went wrong there and
// both are the shape rather than the code:
//
//   * the six-field index line was written by `trashEntryLine` in one place and
//     spelled out again, field by field, in the restore that rewrites the
//     file. A seventh column would have reached the writer and been silently
//     dropped by the rewrite, which is the failure that has no error in it: the
//     entries stay listed and quietly lose a field.
//   * the ordering rule -- the index entry lands before the file moves, so a
//     crash cannot leave a file in the trash that nothing names -- was
//     documented in `deleteNote` and had to be obeyed by `deleteFolder`,
//     `deleteCompanion` and anything added later. It is stated once here, on
//     the two methods that have to be called in that order.
//
// What stays with `Library` is which files a deletion is *about* -- a note's
// attachment directory, every attachment directory under a folder -- because
// that is library knowledge. What is here is the filing.
namespace micronotes::library {

// A note, folder or companion file waiting in the trash. Deletion is a move
// inside the library, so an entry is a real file plus the record of where it
// belongs.
struct TrashEntry {
  std::string name;                       // file name inside `trash/files`
  std::filesystem::path originalRelative; // where it came from, relative to the root
  std::string title;
  std::string deletedAt;                  // local ISO-8601, for the restore list
  // The note's attachment directory, moved and restored with it.
  std::string attachmentName;
  std::filesystem::path attachmentOriginalRelative;
};

class Trash {
public:
  Trash() = default;
  explicit Trash(const std::filesystem::path& root);

  // Creates the trash directory, so a name can be reserved against what is
  // already in it.
  void ensure() const;

  // A free name inside the trash for `original`, recorded in `reserved`.
  //
  // `reserved` is not an optimisation: a folder delete files the folder and
  // every attachment directory under it before a single one of them has moved,
  // so the filesystem check alone hands the same name out twice.
  std::string reserveNameFor(const std::filesystem::path& original,
                             std::vector<std::string>& reserved) const;

  // Adds `entries` to the index, durably, in one write.
  //
  // Call this *before* `fileAs`, always. A crash after the move and before the
  // index leaves a file in the trash that nothing names and nobody can
  // restore; a crash the other way round leaves an index line for a file that
  // never arrived, and `entries()` already drops those. One write for a whole
  // folder's worth of lines rather than one per entry -- and an `ofstream`
  // append with no fsync was what this was, on the only record of where a
  // deleted note came from.
  bool append(const std::vector<TrashEntry>& entries) const;

  // Moves `path` into the trash as `name`. Falls back to a copy-and-remove
  // when the rename crosses a device.
  bool fileAs(const std::filesystem::path& path, const std::string& name) const;

  // Every row of the index, parsed, in the order it was written. The restore
  // needs all of them because it rewrites the whole file.
  std::vector<TrashEntry> readIndex() const;

  // The rows worth offering somebody, newest first: the thing just deleted is
  // the thing most likely wanted back. Drops entries whose file has gone --
  // emptied by hand, or already restored -- and the attachment directories,
  // which are filed so a folder restore can find them and are not something to
  // offer. They are the entries with no title.
  std::vector<TrashEntry> entries() const;

  // Puts an entry back where it came from, renaming around anything that has
  // taken its place, and rewrites the index without it. False when the filed
  // copy is no longer there.
  //
  // `safeRoot` is the library's, because a restore writes into the library and
  // the target comes out of a file on disk: an index line naming `../..`
  // somewhere would otherwise restore outside the root.
  bool restore(const std::string& name, const platform::SafeRoot& safeRoot) const;

  const std::filesystem::path& filesDir() const {
    return files_;
  }

private:
  // The line format, in one place. Both the append and the restore's rewrite go
  // through it; the restore used to spell it out again.
  static std::string entryLine(const TrashEntry& entry);

  // The library root, kept because a restore target is an index line's
  // *relative* path and `SafeRoot::normalize` resolves a relative path against
  // the process's working directory, not against the root it guards.
  std::filesystem::path root_;
  std::filesystem::path files_;
  std::filesystem::path index_;
};

}
