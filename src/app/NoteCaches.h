#pragma once

#include "CoreAliases.h"

#include "core/markdown/MarkdownParser.h"
#include "doc/RenderLayout.h"
#include "library/Organization.h"
#include "ui/Memo.h"
#include "ui/NoteProperties.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// What the shell remembers about the note on the page, and the rules for
// forgetting it.
//
// All four of these were loose fields on `UiRuntime` -- eleven of them between
// the four -- and in each case the *rule* was a comment rather than code. The
// image cache had to be cleared when the library root moved, and the one caller
// did that by hand. The parse cache's "is a sweep due" was arithmetic over two
// fields at its one call site. The wikilink list's header said "see
// `invalidateWikiNotes`, which is the only thing that should touch either",
// which is a comment doing a private member's job.
//
// A cache whose invalidation rule lives at its call site is a cache that is
// correct until it has two call sites.
namespace micronotes::ui {
class AppState;
}

namespace micronotes::app {

// A note and the library revision it was read at: the key every memo derived
// from a note's *front matter* turns on.
//
// The revision is what makes it safe. Rename, tags and icon all go through
// `refreshLibrary`, which moves it, so nothing has to remember to invalidate
// anything -- and a key cannot be forgotten, only unequal.
struct NoteRevision {
  std::string noteId;
  std::uint64_t revision = 0;
  bool operator==(const NoteRevision&) const = default;
};

// The open note's front matter, as the rows drawn above its first block.
using PageHeaderMemo = ui::Memo<std::vector<ui::NoteProperty>, NoteRevision>;

// The blocks the note page hands off to md4c -- tables, footnote definitions,
// anything the block scanner does not model -- parsed and laid out.
//
// Keyed by the block's own source text, and looked up through a view, so
// finding an entry does not first allocate a copy of the bytes to look it up
// by.
//
// It holds the *layout* and not only the parse, because the note page measures
// each of these blocks and then draws it -- the layout asks for a height when
// the block is relaid and the paint asks again every frame -- and a cache of
// only the parse meant shaping every table on screen twice per frame. Laying
// one out again at a width it is already laid out at is a comparison.
class ComplexRenderCache {
public:
  // The entry for `source`, created empty when there is not one yet: a fresh
  // one has no parse and no layout, and the caller fills both in place. Counts
  // the reuse when there was one.
  doc::RenderedBlock& entry(std::string_view source);

  // Whether enough entries have accumulated to be worth walking the note to
  // find out which are dead. One comparison, which is the point: the answer is
  // asked once a frame and the walk it guards is O(blocks).
  bool sweepDue() const;
  // Drops every entry whose source is not in `live`, and takes `live`'s
  // distinct count as the new floor. Takes the vector by value because it has
  // to sort it: two identical tables are one entry, so a live *count* of
  // non-distinct keys makes the next sweep fire a block early, every frame.
  void sweep(std::vector<std::string_view> live);

  // How many entries are held. Observation only: the tests assert that a
  // note's parses are kept across layouts and dropped when the note is
  // replaced, and there is nothing else that says so -- the counters cannot
  // distinguish "not reparsed because it was cached" from "not reparsed
  // because it was not laid out".
  std::size_t size() const { return entries_.size(); }

private:
  std::map<std::string, doc::RenderedBlock, std::less<>> entries_;
  // How many distinct `Complex` blocks the last sweep found in the note. The
  // cache is allowed to run this far past it before the next sweep.
  std::size_t live_ = 0;
};

// Image targets resolved to files on disk, or to nothing when the target names
// no drawable file.
//
// Memoised because resolving one canonicalises both the library root and the
// candidate -- a `stat` per path component of each, twice -- and the layout
// asks per picture per relaid block: a note of 200 pictures spent about 3,200
// syscalls being laid out, which was 47 ms of its first frame.
class ImagePathCache {
public:
  // Points the cache at `root`, dropping everything when that is not where it
  // was pointing. Every lookup goes through this, so a resolved path can never
  // outlive the library it was resolved against -- which is what the one call
  // site used to promise in a comment.
  void retarget(const std::filesystem::path& root);
  // The resolved path for `target`, or null when it has not been resolved.
  // Counts the hit.
  const std::filesystem::path* find(std::string_view target);
  // An empty path is a real answer -- "this target names nothing drawable" --
  // and is kept, so a broken picture is not re-resolved on every layout.
  const std::filesystem::path& keep(std::string_view target, std::filesystem::path path);

private:
  std::map<std::string, std::filesystem::path, std::less<>> entries_;
  std::filesystem::path root_;
};

// Every note in the library, for resolving `[[wikilinks]]`.
//
// Rebuilt on demand rather than on every layout: a note with fifty links would
// otherwise list the whole library fifty times per keystroke. The revision
// moves with every invalidation, so a page can be told that what a `[[target]]`
// resolves to may have changed without being handed the list.
//
// A class rather than three fields because the three have one invariant between
// them -- the revision moves *when and only when* the list is dropped -- and a
// caller that bumps one without the other leaves every page relaying its links
// forever, or none of them ever again.
class WikiTargets {
public:
  const std::vector<library::NoteListItem>& all(const ui::AppState& state);
  void invalidate();
  std::uint64_t revision() const { return revision_; }

private:
  std::vector<library::NoteListItem> notes_;
  bool valid_ = false;
  // Starts at 1 so a page's "the links I laid out were revision 0" cannot
  // accidentally agree with the first real answer.
  std::uint64_t revision_ = 1;
};

}
