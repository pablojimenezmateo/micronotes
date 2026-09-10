#pragma once

#include "core/AppIdentity.h"

#include <filesystem>
#include <string>
#include <string_view>

// Where a library keeps the things that are not notes.
//
// A library root holds the person's notes and one directory of ours --
// `microcore::kAppDotDir`, `.micronotes` here -- with the attachments, the
// trash and the recovery queue under it. Every one of those paths was built by
// concatenating the literal `".micronotes"` at the point of use: six sites
// across `Library.cpp` and `RecoveryStore.cpp`, while `kAppDotDir` existed for
// exactly this and was used by one of them -- the walk that prunes the
// directory. So the tree walk skipped a directory named after the app and the
// code writing into it named a hard-coded one, and the two only agreed because
// nobody had renamed the app.
//
// They are functions rather than constants because each one takes the root, and
// they are here rather than on `Library` because `RecoveryStore` needs one and
// does not have a `Library`.
namespace micronotes::library {

// The library's own directory inside the root. Pruned by `Library::walk`, so
// nothing under it is ever a note.
inline std::filesystem::path stateDir(const std::filesystem::path& root) {
  return root / microcore::kAppDotDir;
}

// One note's attachment directory, named by the note's id. The note's own file
// carries no record of it: the id is the only link, which is why deleting a
// note has to look this up rather than being told.
inline std::filesystem::path attachmentsDir(const std::filesystem::path& root,
                                            std::string_view noteId) {
  return stateDir(root) / "attachments" / std::filesystem::path(noteId);
}

// The parent of every note's attachment directory, created with the layout.
inline std::filesystem::path attachmentsDir(const std::filesystem::path& root) {
  return stateDir(root) / "attachments";
}

// The trash: the filed copies, and the index naming where each came from. See
// `library/Trash.h` for why the trash is in here rather than the desktop's.
inline std::filesystem::path trashFilesDir(const std::filesystem::path& root) {
  return stateDir(root) / "trash" / "files";
}

inline std::filesystem::path trashIndexPath(const std::filesystem::path& root) {
  return stateDir(root) / "trash" / "index";
}

// Where the recovery queue writes the buffer nobody has saved yet.
inline std::filesystem::path recoveryDir(const std::filesystem::path& root) {
  return stateDir(root) / "recovery";
}

}
