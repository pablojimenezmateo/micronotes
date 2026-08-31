#pragma once

#include "doc/BlockScan.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::doc {

// Toggles, without inventing syntax. A block is foldable when the document
// already nests something under it - a heading owns everything down to the next
// heading of its rank, a list item owns the items indented beneath it - so
// folding hides structure the file already states and the bytes never change.
// That is why the fold state lives in `.micronotes/`, not in the note.

// Whether a block of this shape could ever head a fold. Cheap, and asked once
// per block per layout, so the expensive questions are never asked at all of
// the paragraphs that make up most of a note.
bool foldableKind(BlockKind kind);

// One past the last block a fold at `index` hides. Equal to `index + 1` when
// there is nothing nested under it.
std::size_t foldEnd(const std::vector<SourceBlock>& blocks, std::size_t index);

bool foldable(const std::vector<SourceBlock>& blocks, std::size_t index);

// A fold's identity across edits: its shape and its text, not its offset, so
// typing above a folded heading does not silently unfold it. Two blocks written
// identically share a key and therefore fold together.
std::string foldKey(std::string_view source, const SourceBlock& block);

}
