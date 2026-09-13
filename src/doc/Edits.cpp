#include "doc/Edits.h"

#include "doc/EditContext.h"

#include <algorithm>
#include <string>
#include <utility>

namespace micronotes::doc {

std::string blockMarker(BlockKind kind, int level, int listDepth, int ordinal, bool checked,
                        char listMarker) {
  const std::string pad(static_cast<std::size_t>(std::max(0, listDepth)) * 2, ' ');
  // A marker inherited from a neighbouring item is trusted only if it is one of
  // the shapes the scanner recognises for that kind; anything else -- a zero, or
  // an ordered item's `)` asked of a bullet -- falls back to the default.
  const auto punctuation = [listMarker](std::string_view allowed, char fallback) {
    return allowed.find(listMarker) == std::string_view::npos ? fallback : listMarker;
  };
  switch(kind) {
    case BlockKind::Heading:
      return std::string(static_cast<std::size_t>(std::clamp(level, 1, 6)), '#') + " ";
    case BlockKind::Bullet:
      return pad + punctuation("-*+", '-') + " ";
    case BlockKind::Todo:
      return pad + punctuation("-*+", '-') + (checked ? " [x] " : " [ ] ");
    case BlockKind::Ordered:
      return pad + std::to_string(ordinal > 0 ? ordinal : 1) + punctuation(".)", '.') + " ";
    case BlockKind::Quote:
      return "> ";
    case BlockKind::Callout: {
      // `level` picks the alert kind here, the way it picks a heading's rank.
      static constexpr const char* kKinds[] = {"NOTE", "TIP", "IMPORTANT", "WARNING", "CAUTION"};
      return std::string("> [!") + kKinds[static_cast<std::size_t>(std::clamp(level, 0, 4))] + "] ";
    }
    default:
      break;
  }
  return "";
}

Edit turnInto(std::string_view source, std::size_t caret, BlockKind kind, int level,
              BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind == BlockKind::Complex) return edit;
  // Rewriting a block as what it already is does nothing - except where the
  // kind carries a variant, as a heading carries its rank and a callout its
  // alert. Comparing the markers covers both without a case for each.
  if(block.kind == kind && kind != BlockKind::Heading && kind != BlockKind::Callout) return edit;
  if(block.kind == kind &&
     source.substr(block.start, block.contentStart() - block.start) ==
         blockMarker(kind, level, block.listDepth, block.ordinal, block.checked,
                     block.listMarker)) {
    return edit;
  }

  if(kind == BlockKind::Divider) {
    edit.valid = true;
    edit.start = block.start;
    edit.end = block.end();
    edit.text = "---\n";
    edit.cursor = block.start + edit.text.size();
    return edit;
  }

  // The content a fence wraps is the block body; every other kind keeps its
  // payload where the scanner marked it.
  const std::size_t contentStart = block.contentStart();
  const std::size_t contentEnd = std::max(contentStart, block.contentEnd());
  std::string content(source.substr(contentStart, contentEnd - contentStart));

  if(kind == BlockKind::Code) {
    edit.valid = true;
    edit.start = block.start;
    edit.end = block.end();
    std::string body = content;
    if(!body.empty() && body.back() != '\n') body.push_back('\n');
    edit.text = "```\n" + body + "```\n";
    edit.cursor = block.start + 4 + (caret > contentStart ? std::min(caret - contentStart, content.size()) : 0);
    return edit;
  }

  const bool fromList = isListKind(kind) && isListKind(block.kind);
  const int depth = fromList ? block.listDepth : 0;
  const int ordinal = kind == BlockKind::Ordered ? block.ordinal : 0;
  const bool checked = kind == BlockKind::Todo && block.kind == BlockKind::Todo && block.checked;
  // One list kind turning into another keeps the punctuation the author typed,
  // so a `* ` list turned into to-dos comes back as `* [ ] ` rather than losing
  // the bullet it was written with. Ordered and unordered do not share a
  // punctuation, and `blockMarker` drops one that does not fit the kind.
  const std::string marker =
      blockMarker(kind, level, depth, ordinal, checked, fromList ? block.listMarker : 0);

  edit.valid = true;
  if(block.kind == BlockKind::Code) {
    // Un-fencing: the body survives verbatim, the new marker leads its first line.
    edit.start = block.start;
    edit.end = block.end();
    edit.text = marker + content;
    edit.cursor = shiftedCaret(std::min(std::max(caret, contentStart), contentEnd), contentStart,
                               block.start + marker.size());
    return edit;
  }
  edit.start = block.start;
  edit.end = contentStart;
  edit.text = marker;
  edit.cursor = shiftedCaret(caret, contentStart, block.start + marker.size());
  return edit;
}

Edit toggleTodo(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind == BlockKind::Todo) {
    // Walk back from the content to the "[" the scanner already validated.
    std::size_t state = block.contentStart();
    while(state > block.start && source[state - 1] != '[') --state;
    if(state == block.start || state >= source.size()) return edit;
    edit.valid = true;
    edit.start = state;
    edit.end = state + 1;
    edit.text = block.checked ? " " : "x";
    edit.cursor = caret;
    return edit;
  }
  if(block.kind == BlockKind::Bullet) {
    edit.valid = true;
    edit.start = block.contentStart();
    edit.end = block.contentStart();
    edit.text = "[ ] ";
    edit.cursor = caret >= block.contentStart() ? caret + edit.text.size() : caret;
    return edit;
  }
  return turnInto(source, caret, BlockKind::Todo, 1, blocks);
}

Edit indent(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(!isListKind(block.kind)) return edit;

  // Markdown can only nest an item under a sibling that already exists.
  bool allowed = false;
  for(std::size_t j = context.index; j > 0; --j) {
    const SourceBlock& previous = context.blocks[j - 1];
    if(previous.kind == BlockKind::Blank) continue;
    allowed = isListKind(previous.kind) && previous.listDepth >= block.listDepth;
    break;
  }
  if(!allowed) return edit;

  edit.valid = true;
  edit.start = block.start;
  edit.end = block.start;
  edit.text = "  ";
  edit.cursor = caret + edit.text.size();
  return edit;
}

Edit outdent(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(!isListKind(block.kind)) return edit;
  const std::size_t whitespace = leadingWhitespace(source, block);
  if(whitespace == 0) return edit;
  const std::size_t remove = source[block.start] == '\t' ? 1 : std::min<std::size_t>(2, whitespace);
  edit.valid = true;
  edit.start = block.start;
  edit.end = block.start + remove;
  edit.cursor = caret > block.start + remove ? caret - remove : block.start;
  return edit;
}

namespace {

// A drop at a chosen boundary: lift the blocks [first, last] together with the
// blank run that separates them from their neighbour, then drop the pair at
// `dest`. The separator travels on the group's trailing side going up and its
// leading side going down, which is what stops two paragraphs from merging into
// one when the group lands against another block.
//
// Reached from the block *drag* only. A one-step move up or down is
// `swapWithNeighbour` below, which needs none of this -- see the note there for
// what carrying the run cost when the step went through here too.
Edit moveGroup(std::string_view source, const BlockList& blocks, std::size_t first,
               std::size_t last, std::size_t dest, std::size_t caret, bool selects) {
  Edit edit;
  const std::size_t gs = blocks[first].start;
  const std::size_t ge = blocks[last].end();
  if(ge <= gs) return edit;

  const Separator run = separatorFor(blocks, first, last);
  const std::size_t liftStart = run.leading ? run.start : gs;
  const std::size_t liftEnd = run.leading ? ge : run.end;

  dest = std::min(dest, source.size());
  if(dest >= liftStart && dest <= liftEnd) return edit;

  std::string groupText(source.substr(gs, ge - gs));
  const std::string separator(source.substr(run.start, run.end - run.start));
  if(groupText.empty() || groupText.back() != '\n') groupText.push_back('\n');
  // The boundary the group is dropped at already separates what sits before it,
  // so the run it carries goes on its far side - unless nothing follows, in
  // which case the group needs the separation in front of it instead.
  const bool separatorAfter = dest < source.size();
  const bool up = dest < liftStart;
  const std::string moved = separatorAfter ? groupText + separator : separator + groupText;

  const std::size_t regionStart = std::min(liftStart, dest);
  const std::size_t regionEnd = std::max(liftEnd, dest);
  const bool regionEndsWithNewline = source[regionEnd - 1] == '\n';

  std::string out;
  std::size_t movedAt = regionStart;
  if(up) {
    out = moved + std::string(source.substr(dest, liftStart - dest));
  } else {
    std::string head(source.substr(liftEnd, dest - liftEnd));
    if(!head.empty() && head.back() != '\n') head.push_back('\n');
    movedAt = regionStart + head.size();
    out = head + moved;
  }
  // The region keeps whatever ending it had, so a note without a final newline
  // does not grow one.
  if(!regionEndsWithNewline && !out.empty() && out.back() == '\n') out.pop_back();

  const std::size_t limit = regionStart + out.size();
  const std::size_t groupAt = separatorAfter ? movedAt : movedAt + separator.size();
  const std::size_t within = caret >= gs && caret < ge ? caret - gs : 0;
  edit.valid = true;
  edit.start = regionStart;
  edit.end = regionEnd;
  edit.text = out;
  edit.anchor = movedAt;
  edit.cursor = std::min(selects ? movedAt + moved.size() : groupAt + within, limit);
  edit.selects = selects;
  return edit;
}

// One step up or down: the group changes places with its nearest neighbour, and
// the blank run between them stays exactly where it is.
//
// A *swap*, not a lift and a drop, which is what `moveGroup` above does for a
// drag to a chosen boundary. The step used to go through that too, and carried
// the blank run along with the group; which side of the group the run landed on
// then had to be decided, and no single rule for it is right in both
// directions. So one direction always tore a hole and filled another:
//
//   * `one\n\ntwo\n\nthree`, first paragraph moved down, came back as
//     `two\none\n\n\nthree` -- "one" merged into "two", and the blank line it
//     had been carrying stacked onto the next one's. Two blank lines out of one.
//   * `- a\n- b\n\npara`, second item moved up, came back as
//     `- b\n\n- a\npara` -- a blank line pushed between two list items, the one
//     below pulled out, the list split and the item merged into the paragraph.
//
// Neither fault is about paragraphs merging, which is what carrying the run was
// for. The run is a *position* in the document, and swapping the two blocks
// either side of it cannot merge anything that was not merged already: the
// arrangement of blank runs between positions comes out exactly as it went in.
// So there is nothing to carry, and nothing that can be invented or swallowed
// -- the region keeps its byte count, its blank lines and its final newline.
Edit swapWithNeighbour(std::string_view source, const BlockList& blocks, std::size_t first,
                       std::size_t last, int delta, std::size_t caret, bool selects) {
  Edit edit;
  const std::size_t gs = blocks[first].start;
  const std::size_t ge = blocks[last].end();
  if(ge <= gs) return edit;

  const auto blankRun = [&blocks](std::size_t i) {
    // The synthetic last-line block has no width, so it separates nothing --
    // and is not a block anything can change places with either.
    return blocks[i].kind == BlockKind::Blank && blocks[i].end() > blocks[i].start;
  };
  // The neighbour, found past whatever blank run lies between, and that run.
  std::size_t neighbour = 0;
  std::size_t gapStart = 0;
  std::size_t gapEnd = 0;
  if(delta < 0) {
    std::size_t j = first;
    while(j > 0 && blankRun(j - 1)) --j;
    if(j == 0) return edit;
    neighbour = j - 1;
    gapStart = blocks[neighbour].end();
    gapEnd = gs;
  } else {
    std::size_t j = last;
    while(j + 1 < blocks.size() && blankRun(j + 1)) ++j;
    if(j + 1 >= blocks.size()) return edit;
    neighbour = j + 1;
    gapStart = ge;
    gapEnd = blocks[neighbour].start;
  }
  const std::size_t otherStart = blocks[neighbour].start;
  const std::size_t otherEnd = blocks[neighbour].end();
  // Nothing to change places with: the group is already against the end it was
  // sent towards, and the only thing past it is the empty last line.
  if(otherEnd <= otherStart) return edit;

  const std::size_t regionStart = delta < 0 ? otherStart : gs;
  const std::size_t regionEnd = delta < 0 ? ge : otherEnd;
  const bool regionEndsWithNewline = source[regionEnd - 1] == '\n';

  std::string out;
  out.reserve(regionEnd - regionStart + 2);
  std::size_t groupAt = regionStart;
  std::size_t groupLength = 0;
  const auto append = [&](std::size_t from, std::size_t to, bool isGroup) {
    const std::size_t at = out.size();
    if(isGroup) groupAt = regionStart + at;
    out.append(source.substr(from, to - from));
    // Every piece has to end the line it sits on, or the piece after it runs
    // onto the same line -- which is the one way a swap could merge two blocks
    // that were not merged before. The last piece's ending is corrected below.
    if(out.empty() || out.back() != '\n') out.push_back('\n');
    if(isGroup) groupLength = out.size() - at;
  };
  if(delta < 0) {
    append(gs, ge, true);
    append(gapStart, gapEnd, false);
    append(otherStart, otherEnd, false);
  } else {
    append(otherStart, otherEnd, false);
    append(gapStart, gapEnd, false);
    append(gs, ge, true);
  }
  // The region keeps whatever ending it had, so a note without a final newline
  // does not grow one.
  if(!regionEndsWithNewline && !out.empty() && out.back() == '\n') {
    out.pop_back();
    if(groupAt + groupLength > regionStart + out.size()) --groupLength;
  }

  const std::size_t limit = regionStart + out.size();
  // Where in the group the caret was, so it comes out of the move still against
  // the same word rather than at the top of the block.
  const std::size_t within = caret >= gs && caret < ge ? caret - gs : 0;
  edit.valid = true;
  edit.start = regionStart;
  edit.end = regionEnd;
  edit.text = out;
  edit.anchor = groupAt;
  edit.cursor = std::min(selects ? groupAt + groupLength : groupAt + within, limit);
  edit.selects = selects;
  return edit;
}

}

Edit moveBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret, int delta,
                BlockSpan blocks) {
  Edit edit;
  if(delta == 0) return edit;
  const Range range = rangeAt(source, fromCaret, toCaret, blocks);
  if(!range.valid) return edit;
  const BlockList& partition = range.blocks;
  if(range.first == range.last && partition[range.first].kind == BlockKind::Blank) return edit;
  return swapWithNeighbour(source, partition, range.first, range.last, delta,
                           std::min(fromCaret, toCaret), fromCaret != toCaret);
}

Edit moveBlock(std::string_view source, std::size_t caret, int delta, BlockSpan blocks) {
  return moveBlocks(source, caret, caret, delta, blocks);
}

Edit duplicateBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                     BlockSpan blocks) {
  Edit edit;
  const Range range = rangeAt(source, fromCaret, toCaret, blocks);
  if(!range.valid) return edit;
  if(range.first == range.last && range.blocks[range.first].kind == BlockKind::Blank) return edit;

  const Separator run = separatorFor(range.blocks, range.first, range.last);
  const std::string copy(source.substr(range.start, range.end - range.start));
  // The copy needs the same separation from the original that the original has
  // from its neighbour, and a block that ends the file without a newline needs
  // one before the copy can start.
  std::string prefix = range.end > 0 && source[range.end - 1] == '\n' ? "" : "\n";
  prefix += std::string(source.substr(run.start, run.end - run.start));

  edit.valid = true;
  edit.start = range.end;
  edit.end = range.end;
  edit.text = prefix + copy;
  const std::size_t copyStart = range.end + prefix.size();
  const std::size_t caret = std::min(fromCaret, toCaret);
  const std::size_t within = caret >= range.start && caret < range.end ? caret - range.start : 0;
  edit.selects = fromCaret != toCaret;
  edit.anchor = copyStart;
  edit.cursor = edit.selects ? copyStart + copy.size() : copyStart + within;
  return edit;
}

Edit duplicateBlock(std::string_view source, std::size_t caret, BlockSpan blocks) {
  return duplicateBlocks(source, caret, caret, blocks);
}

Edit deleteBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                  BlockSpan blocks) {
  Edit edit;
  const Range range = rangeAt(source, fromCaret, toCaret, blocks);
  if(!range.valid) return edit;
  // The blank line that separated the blocks goes with them; leaving it behind
  // grows a run of empty lines every time a block is removed.
  const Separator run = separatorFor(range.blocks, range.first, range.last);
  edit.valid = true;
  edit.start = run.leading ? run.start : range.start;
  edit.end = run.leading ? range.end : run.end;
  edit.cursor = edit.start;
  edit.anchor = edit.start;
  return edit;
}

Edit deleteBlock(std::string_view source, std::size_t caret, BlockSpan blocks) {
  return deleteBlocks(source, caret, caret, blocks);
}

Edit turnBlocksInto(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                    BlockKind kind, int level, BlockSpan blocks) {
  Edit edit;
  const Range range = rangeAt(source, fromCaret, toCaret, blocks);
  // The empty last line spans no bytes, but it is still somewhere a block can
  // be started - which is exactly what the slash menu does there.
  if(!range.valid) return turnInto(source, fromCaret, kind, level);

  // Work on the span alone, and on the span's *own* bytes rather than on a
  // buffer this loop keeps rewriting. Every rewrite `turnInto` produces lands
  // inside the block it came from -- it replaces either the marker or the whole
  // block, never anything outside it -- so taken front to back the rewrites are
  // disjoint and in increasing order, and the answer is a concatenation.
  //
  // That is what lets the chunk's partition be lent to every call: `chunk` is
  // never modified, so its partition never stops describing it. This used to
  // rewrite the chunk in place, which cost a rescan *and* a vector allocation
  // per block and made the loop's direction load-bearing -- back to front was
  // the only order in which the offsets it read were still true. Now nothing
  // depends on the direction, and nothing is scanned twice.
  const std::string chunk(source.substr(range.start, range.end - range.start));
  const auto chunkBlocks = scanBlocks(chunk);
  std::string out;
  out.reserve(chunk.size() + 16);
  std::size_t copied = 0;
  bool changed = false;
  for(const auto& block : chunkBlocks) {
    if(block.kind == BlockKind::Blank) continue;
    const Edit one = turnInto(chunk, block.contentStart(), kind, level, chunkBlocks);
    if(!one.valid) continue;
    // Disjoint and in order, per the argument above. A rewrite that reached
    // back into bytes already copied would mean two blocks claiming the same
    // ones; the guard says so rather than corrupting the output if it ever does.
    if(one.start < copied) continue;
    out.append(chunk, copied, one.start - copied);
    out.append(one.text);
    copied = one.end;
    changed = true;
  }
  // A range holding nothing but blank lines has no block to rewrite - but the
  // caret still sits somewhere a block can be started.
  if(!changed) return turnInto(source, fromCaret, kind, level, blocks);
  out.append(chunk, copied, chunk.size() - copied);

  edit.valid = true;
  edit.start = range.start;
  edit.end = range.end;
  edit.anchor = range.start;
  edit.cursor = range.start + out.size();
  edit.selects = true;
  edit.text = std::move(out);
  return edit;
}

Edit insertBlockAfter(std::string_view source, std::size_t caret, BlockKind kind, int level,
                      BlockSpan blocks) {
  Edit edit;
  const Range range = rangeAt(source, caret, caret, blocks);
  std::size_t at = source.size();
  std::string separator;
  if(range.valid) {
    const Separator run = separatorFor(range.blocks, range.first, range.last);
    at = run.leading ? range.end : run.end;
    // A trailing separator is reproduced after the new block, so the block that
    // used to follow stays as far away as it was.
    if(!run.leading) separator = std::string(source.substr(run.start, run.end - run.start));
  }

  std::string body;
  if(kind == BlockKind::Divider) body = "---\n";
  else if(kind == BlockKind::Code) body = "```\n\n```\n";
  else body = blockMarker(kind, level, 0, 1, false) + "\n";

  const std::string prefix = at > 0 && source[at - 1] != '\n' ? "\n" : "";
  edit.valid = true;
  edit.start = at;
  edit.end = at;
  edit.text = prefix + body + separator;
  const std::size_t bodyStart = at + prefix.size();
  edit.cursor = kind == BlockKind::Code ? bodyStart + 4 : bodyStart + body.size() - 1;
  edit.anchor = edit.cursor;
  return edit;
}

Edit moveBlocksTo(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                  std::size_t destination, BlockSpan blocks) {
  const Range range = rangeAt(source, fromCaret, toCaret, blocks);
  if(!range.valid) return {};
  return moveGroup(source, range.blocks, range.first, range.last, destination, std::min(fromCaret, toCaret),
                   fromCaret != toCaret);
}

Edit wrapSelection(std::string_view source, std::size_t start, std::size_t end,
                   std::string_view open, std::string_view close) {
  Edit edit;
  if(start > end) std::swap(start, end);
  if(end > source.size() || open.empty()) return edit;

  // Already wrapped, markers just outside the selection: this is a toggle off.
  if(start >= open.size() && end + close.size() <= source.size() &&
     source.substr(start - open.size(), open.size()) == open &&
     source.substr(end, close.size()) == close) {
    edit.valid = true;
    edit.start = start - open.size();
    edit.end = end + close.size();
    edit.text = std::string(source.substr(start, end - start));
    edit.anchor = edit.start;
    edit.cursor = edit.start + edit.text.size();
    edit.selects = end > start;
    return edit;
  }
  // Already wrapped, markers inside the selection.
  if(end - start >= open.size() + close.size() &&
     source.substr(start, open.size()) == open &&
     source.substr(end - close.size(), close.size()) == close) {
    edit.valid = true;
    edit.start = start;
    edit.end = end;
    edit.text = std::string(source.substr(start + open.size(), end - start - open.size() - close.size()));
    edit.anchor = start;
    edit.cursor = start + edit.text.size();
    edit.selects = !edit.text.empty();
    return edit;
  }

  const std::string inner(source.substr(start, end - start));
  edit.valid = true;
  edit.start = start;
  edit.end = end;
  edit.text = std::string(open) + inner + std::string(close);
  edit.anchor = start + open.size();
  edit.cursor = edit.anchor + inner.size();
  edit.selects = !inner.empty();
  return edit;
}

Edit makeLink(std::string_view source, std::size_t start, std::size_t end, std::string_view target) {
  Edit edit;
  if(start > end) std::swap(start, end);
  if(end > source.size()) return edit;
  const std::string inner(source.substr(start, end - start));
  edit.valid = true;
  edit.start = start;
  edit.end = end;
  edit.text = "[" + inner + "](" + std::string(target) + ")";
  // With no destination yet, the caret waits between the parentheses.
  edit.cursor = target.empty() ? start + inner.size() + 3 : start + edit.text.size();
  edit.anchor = edit.cursor;
  return edit;
}

}
