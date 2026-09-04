#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string_view>

namespace microcore::platform {

// Cheap on-disk identity of a file: modification time and size, from one stat.
//
// This is what says "the file I am about to overwrite is still the file I read".
// A content hash would be exact and would cost a read of the whole file; mtime
// plus size is one syscall and every writer that is not deliberately hiding
// bumps one of the two. The blind spot is a rewrite that keeps the byte length
// *and* lands inside the same mtime tick -- nanosecond resolution on ext4, so
// it takes a machine trying to.
//
// `exists == false, error == false` means the file is genuinely absent.
// `error == true` means the stat itself failed, which is treated as "changed"
// everywhere: refusing to assume is the safe direction when the question is
// whether it is about to be destroyed.
struct FileSignature {
  bool exists = false;
  bool error = false;
  std::uint64_t mtimeNanos = 0;
  std::uint64_t size = 0;

  // Two existing files with the same mtime and size are the same content.
  // False whenever either side is absent or unknown, so a missing baseline
  // never reads as agreement.
  bool sameContentAs(const FileSignature& other) const {
    return exists && other.exists && !error && !other.error && mtimeNanos == other.mtimeNanos &&
           size == other.size;
  }
};

FileSignature statFile(const std::filesystem::path& path);

// Writes `contents` to `path` so that a crash leaves either the previous file
// or the whole new one, never a half-written one: a unique temp beside the
// target, fsync, rename, fsync of the directory.
//
// Three things it preserves, each of which it used to destroy:
//
// - **The symlink.** A note that is a symlink into another tree was replaced by
//   a regular file, so the link died and the edits went somewhere the user was
//   no longer pointing at. The chain is followed and its final target is what
//   gets written.
// - **The mode and the owner.** A rename puts a *fresh* inode in place, so a
//   note the user had at 0600 came back 0644 on the first autosave. The
//   replaced file's mode and ownership are carried onto the new one.
// - **Nothing about the directory listing.** The temp is dot-prefixed and
//   carries a distinctive marker, so a tree walk, a file sync daemon or the
//   library index can recognise it rather than filing it as a note.
bool writeFileDurably(const std::filesystem::path& path, std::string_view contents);

// The same write, from pieces, so a header and a body do not have to be
// concatenated into a third buffer first. `saveNote` did exactly that: one
// allocation and one copy of the whole note per save, for the sake of gluing a
// hundred-byte header to the front of it.
bool writeFileDurably(const std::filesystem::path& path,
                      std::initializer_list<std::string_view> pieces);

bool removeFileDurably(const std::filesystem::path& path);

// Marks a file as an in-flight durable-write temp. Distinctive rather than a
// bare `.tmp`, because a library may legitimately hold `notes.tmp.md`, and
// because a temp left behind by a crash has to be recognisable as debris a year
// later.
inline constexpr std::string_view kTemporaryWriteMarker = ".microcore-write.";

// True for a file name produced by this unit's staging. Lives beside the
// generator so the naming and the filtering cannot drift apart.
bool isTemporaryWriteName(std::string_view fileName);

}
