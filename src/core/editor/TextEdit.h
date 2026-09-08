#pragma once

#include <cstddef>
#include <cstdint>

// One edit to a text buffer, stamped with the revisions it stands between.
//
// The point of this being a *value* rather than a promise is the two stamps.
// A consumer that is handed one can **check** rather than trust: `from` must be
// the revision of the buffer it is standing on and `to` the revision of the
// text it has just been given, and a pair that does not match what it holds --
// two edits landed between its frames, or it skipped one -- is discarded, and
// the consumer falls back to comparing bytes. That fallback is what makes the
// whole arrangement safe to be wrong about.
//
// `to == 0` means "cannot say", which is what a caller with no edit to offer
// gets: a test, the perf harness, the first frame after a note opens.
//
// This was declared twice, once as `MarkdownEditor::TextChange` and once as
// `editor::TextEdit`, with the same five fields and the same
// contract described in two places -- and the conversion between them written
// out at two more, in the live page and the reading page, each of them applying
// the same `+1` shift. Five identical fields in two structs is a struct that
// wants a name.
namespace microcore::editor {

struct TextEdit {
  std::uint64_t fromRevision = 0;
  std::uint64_t toRevision = 0;
  std::size_t start = 0;
  std::size_t oldEnd = 0;
  std::size_t newEnd = 0;

  // Whether there is an edit to describe at all.
  bool known() const { return toRevision != 0; }

  // The same edit in a revision space shifted by `by`.
  //
  // The surfaces stamp their layouts with `revision() + 1`, because zero is
  // their "cannot say", so a claim about the editor's revisions has to be
  // moved into that space before a layout can check it. Doing that at the two
  // call sites meant two copies of an off-by-one waiting to disagree.
  TextEdit shiftedBy(std::uint64_t by) const {
    if(!known()) return {};
    return {fromRevision + by, toRevision + by, start, oldEnd, newEnd};
  }
};

}
