#include "ui/Outline.h"

#include "doc/BlockScan.h"

#include "core/perf/PerformanceCounters.h"
#include "core/util/BandSplice.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace micronotes::ui {

namespace {

// Indentation follows the shape of the note. A run of h3s under nothing is a
// flat list; an h3 under an h2 is one step in. The stack holds the levels
// currently open, so a heading that closes several at once steps back out.
//
// Over the entries rather than over the blocks, so it costs the number of
// headings and not the size of the note -- which is why the splice below can
// redo it wholesale instead of trying to patch it. A note with a thousand
// headings has a thousand of these; it has ten thousand blocks.
void applyDepths(std::vector<OutlineEntry>& entries) {
  std::vector<int> open;
  for(auto& entry : entries) {
    while(!open.empty() && open.back() >= entry.level) open.pop_back();
    entry.depth = static_cast<int>(open.size());
    open.push_back(entry.level);
  }
}

// One heading from the block that is one, appended.
void appendHeading(std::string_view source, const doc::SourceBlock& block,
                   std::vector<OutlineEntry>& entries) {
  OutlineEntry entry;
  entry.level = std::clamp<int>(block.level, 1, 6);
  entry.offset = block.contentStart();
  entry.text =
    std::string(source.substr(block.contentStart(), block.contentEnd() - block.contentStart()));
  // A closing run of hashes is decoration in ATX headings and is not part of
  // the title.
  while(!entry.text.empty() && (entry.text.back() == '#' || entry.text.back() == ' ')) {
    entry.text.pop_back();
  }
  if(entry.text.empty()) entry.text = "Untitled section";
  entries.push_back(std::move(entry));
}

// Fills `entries` from a partition, whichever way the caller got one.
void buildOutline(std::string_view source, doc::BlockSpan blocks,
                  std::vector<OutlineEntry>& entries) {
  // The block scan already knows the difference between a heading and a line
  // that starts with a hash inside a fence, so the outline asks it rather than
  // scanning for "#" itself and getting that wrong in a second place.
  // Tallied in one go rather than per block: the counter is a relaxed atomic
  // add and this loop is the thing being counted.
  microcore::perf::addCounter(microcore::perf::CounterId::RightPanelOutlineBlocksWalked,
                              blocks.size());
  for(const auto& block : blocks) {
    if(block.kind != doc::BlockKind::Heading) continue;
    appendHeading(source, block, entries);
  }

  applyDepths(entries);
}

}

void outlineInto(std::string_view source, doc::BlockSpan blocks,
                 std::vector<OutlineEntry>* out) {
  std::vector<OutlineEntry>& entries = *out;
  // Cleared rather than replaced: the vector and its entries' strings keep the
  // capacity they had, which on a note whose outline barely changes between
  // keystrokes is every allocation this used to make.
  entries.clear();
  if(!blocks.empty()) {
    buildOutline(source, blocks, entries);
    return;
  }
  // No partition lent, so derive one. Exactly what every caller got before.
  const std::vector<doc::SourceBlock> scanned = doc::scanBlocks(source);
  buildOutline(source, doc::BlockSpan(scanned), entries);
}

bool outlineUpdate(std::string_view source, doc::BlockSpan blocks, const doc::BlockRelay& relay,
                   std::uint64_t builtAtRevision, std::vector<OutlineEntry>* entries) {
  if(!relay.leadsFrom(builtAtRevision) || blocks.empty()) return false;
  // The relay describes a splice of *this* partition, so the bands it names
  // have to be inside it. A caller that borrowed the blocks from the same
  // layout cannot fail this; one that scanned its own can, and it must not be
  // spliced against somebody else's splice.
  const std::size_t count = blocks.size();
  if(relay.headBlocks + relay.tailBlocks > count) return false;

  std::vector<OutlineEntry>& out = *entries;

  // Three bands, in order, so the result is built once and in place.
  //
  // The head keeps its entries untouched -- nothing under `headEnd` moved, in
  // either buffer. `std::partition_point` finds where it ends because the
  // entries are in offset order by construction, which is the one invariant
  // this whole splice rests on.
  const auto headEntries = static_cast<std::size_t>(
    std::partition_point(out.begin(), out.end(),
                         [&](const OutlineEntry& entry) { return entry.offset < relay.headEnd; }) -
    out.begin());

  // The tail's entries are the ones that were at or past where the carried tail
  // used to start. Found in the *old* offsets, which is what `out` still holds.
  //
  // `tailBlocks == 0` is checked rather than derived, and that is a bug this
  // test caught rather than a precaution. With nothing carried at the back the
  // relay reports `tailStart` as the end of the buffer, and the end of the *new*
  // buffer minus the shift is the end of the old one -- so an entry sitting in
  // the very last block read as "in the carried tail" and was kept, shifted,
  // when the block it came from had just been re-derived out of existence.
  // Nothing carried means no entries carried, and nothing about a byte offset
  // says that.
  const auto oldTailStart =
    static_cast<std::size_t>(static_cast<std::ptrdiff_t>(relay.tailStart) - relay.byteShift);
  const std::size_t tailFrom =
    relay.tailBlocks == 0
      ? out.size()
      : static_cast<std::size_t>(
          std::partition_point(
            out.begin() + static_cast<std::ptrdiff_t>(headEntries), out.end(),
            [&](const OutlineEntry& entry) { return entry.offset < oldTailStart; }) -
          out.begin());

  // The middle is the only part read off the blocks, and it is the whole point:
  // for a typed character it is three blocks rather than the note's ten
  // thousand.
  const doc::BlockSpan middle =
    blocks.subspan(relay.headBlocks, count - relay.tailBlocks - relay.headBlocks);
  microcore::perf::addCounter(microcore::perf::CounterId::RightPanelOutlineBlocksWalked,
                              middle.size());
  std::vector<OutlineEntry> replaced;
  for(const auto& block : middle) {
    if(block.kind != doc::BlockKind::Heading) continue;
    appendHeading(source, block, replaced);
  }

  // The tail moves in place. Copying it out and back is what the first version
  // did, and on a note with five hundred headings that was a five-hundred-entry
  // vector -- 25 KB, allocated and freed -- per typed character, to carry
  // forward entries that needed one integer added to each. An offset is the
  // only thing about a carried heading that moved.
  for(std::size_t i = tailFrom; i < out.size(); ++i) {
    out[i].offset =
      static_cast<std::size_t>(static_cast<std::ptrdiff_t>(out[i].offset) + relay.byteShift);
  }
  // And the middle is spliced rather than rebuilt on top of. The tail's entries
  // are carried by move, which for a type holding a `std::string` is a pointer
  // swap each and no allocation at all -- and `replaced` is empty for any
  // keystroke that did not touch a heading, which is nearly all of them.
  // `erase` then `insert` was what this said, and it moved that tail twice.
  microcore::util::replaceBand(out, headEntries, tailFrom - headEntries,
                               std::make_move_iterator(replaced.begin()),
                               std::make_move_iterator(replaced.end()));

  // A heading added or removed in the middle changes the indentation of
  // everything under it, so there is no bounded answer to want and this redoes
  // the lot -- but only when the middle actually moved a heading. A keystroke
  // inside a paragraph removes no entry and adds none, and then every level in
  // the list is the one it already had, in the order it already had. That is
  // nearly every keystroke, and the walk is O(headings) with an allocation in
  // it: on the fixture's five hundred headings it was the whole of what was
  // left of this function.
  if(!replaced.empty() || headEntries != tailFrom) applyDepths(out);
  return true;
}

std::vector<OutlineEntry> outlineOf(std::string_view source) {
  std::vector<OutlineEntry> entries;
  outlineInto(source, {}, &entries);
  return entries;
}

std::size_t outlineEntryAt(const std::vector<OutlineEntry>& entries, std::size_t caret) {
  std::size_t found = std::numeric_limits<std::size_t>::max();
  for(std::size_t i = 0; i < entries.size(); ++i) {
    if(entries[i].offset > caret) break;
    found = i;
  }
  return found;
}

}
