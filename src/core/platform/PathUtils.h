#pragma once

#include <filesystem>
#include <string>

namespace microcore::platform {

struct RuntimePaths {
  std::filesystem::path configDir;
  std::filesystem::path cacheDir;
  std::filesystem::path dataDir;
};

RuntimePaths resolveRuntimePaths();

// A root directory, canonicalized once, that paths can be checked against.
//
// The containment check has to canonicalize both sides -- that is what makes it
// a check rather than a string comparison a `..` walks straight through. But
// canonicalizing is a `stat` per component, and `normalizeInsideRoot` did it to
// *both* sides on every call, so the root's half was recomputed once per note
// for an answer that cannot change while a library is open. Measured on a
// shallow root it was 3.4 us of the 14.6 us a note read costs, and it grows
// with the depth of the root.
//
// So the root is resolved when the object is made and the candidate on each
// call. A library's root does not move underneath it while it is open, which is
// the invariant this puts in the type instead of leaving to discipline.
class SafeRoot {
public:
  SafeRoot() = default;
  explicit SafeRoot(const std::filesystem::path& root);

  // The canonical form of `candidate`. Throws `std::runtime_error` when it
  // falls outside the root: refusing is the whole point, so there is no form of
  // this that returns a path the caller then has to remember to check.
  std::filesystem::path normalize(const std::filesystem::path& candidate) const;

private:
  std::filesystem::path canonical_;
};

// The same check for a caller with nowhere to keep a `SafeRoot` -- it resolves
// the root on every call, which is what this cost before there was one.
std::filesystem::path normalizeInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate);
std::string sanitizeFileStem(std::string title);

// A path as a person reads it: `~` for the home directory, as every other tool
// that prints one does. For status lines, prompts and the settings list -- never
// for anything that then opens the file.
std::string displayPath(const std::filesystem::path& path);

// `desired`, or the first `name-2`, `name-3`... beside it that nothing holds.
//
// `keep` is a path that does not count as taken: a rename to a name the file
// already has must not suffix itself. Compared by `equivalent`, so a
// case-insensitive or symlinked filesystem answers correctly.
//
// The extension is preserved exactly, *including* an absent one -- which is
// what the three hand-written copies of this could not agree on. One defaulted
// to `.md` when there was none, on the reasoning that a note is a Markdown
// file; the restore path then had to spell the whole loop out again, on a
// comment saying it could not use the note helper because it "assumes a `.md`
// file and would give a restored folder an extension". The default was the
// problem, so it is gone: a caller that wants `.md` is the caller that knows
// it, and says so in the path it asks for.
std::filesystem::path uniquePath(const std::filesystem::path& desired,
                                 const std::filesystem::path& keep = {});

}
