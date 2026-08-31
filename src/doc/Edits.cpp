#include "doc/Edits.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace micronotes::doc {
namespace {

struct Context {
  std::vector<SourceBlock> blocks;
  std::size_t index = 0;
};

// A buffer ending in a newline leaves one caret position no block covers: the
// empty last line. Give it a block of its own, so Enter there starts a
// paragraph instead of continuing the list above it, and so the block commands
// find nothing there to act on.
std::vector<SourceBlock> scanBlocksWithLastLine(std::string_view source) {
  auto blocks = scanBlocks(source);
  if(!source.empty() && source.back() == '\n') {
    SourceBlock trailing;
    trailing.kind = BlockKind::Blank;
    trailing.start = source.size();
    trailing.end = source.size();
    trailing.contentStart = source.size();
    trailing.contentEnd = source.size();
    blocks.push_back(trailing);
  }
  return blocks;
}

Context contextAt(std::string_view source, std::size_t caret) {
  Context context;
  context.blocks = scanBlocksWithLastLine(source);
  context.index = blockIndexAt(context.blocks, std::min(caret, source.size()));
  return context;
}

std::size_t leadingWhitespace(std::string_view source, const SourceBlock& block) {
  std::size_t i = block.start;
  while(i < block.end && (source[i] == ' ' || source[i] == '\t')) ++i;
  return i - block.start;
}

std::size_t lineEndFrom(std::string_view source, std::size_t start) {
  const auto newline = source.find('\n', start);
  return newline == std::string_view::npos ? source.size() : newline;
}

// Keeps a caret that sat inside the block's content pointing at the same
// character after the marker in front of it changed length.
std::size_t shiftedCaret(std::size_t caret, std::size_t contentStart, std::size_t newContentStart) {
  if(caret < contentStart) return newContentStart;
  return newContentStart + (caret - contentStart);
}

// The span of whole blocks two carets reach across. Unlike `contextAt` this
// never invents a trailing block: a range operation on the empty last line has
// nothing to act on, and says so.
struct Range {
  bool valid = false;
  std::vector<SourceBlock> blocks;
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t start = 0;
  std::size_t end = 0;
};

// The blank run that separates [first, last] from its neighbour: the one after
// the group when there is one, otherwise the one before it. Every block command
// carries this run along, because dropping two paragraphs against each other
// with no blank line between them merges them into one.
struct Separator {
  std::size_t start = 0;
  std::size_t end = 0;
  bool leading = false;  // the run sits before the group rather than after it
};

Separator separatorFor(const std::vector<SourceBlock>& blocks, std::size_t first, std::size_t last) {
  Separator separator;
  separator.start = separator.end = blocks[last].end;
  const auto blankRun = [&blocks](std::size_t i) {
    // The synthetic last-line block has no width, so it separates nothing.
    return blocks[i].kind == BlockKind::Blank && blocks[i].end > blocks[i].start;
  };
  std::size_t after = last;
  while(after + 1 < blocks.size() && blankRun(after + 1)) ++after;
  if(after > last) {
    separator.start = blocks[last].end;
    separator.end = blocks[after].end;
    return separator;
  }
  std::size_t before = first;
  while(before > 0 && blankRun(before - 1)) --before;
  if(before < first) {
    separator.start = blocks[before].start;
    separator.end = blocks[first].start;
    separator.leading = true;
  }
  return separator;
}

Range rangeAt(std::string_view source, std::size_t from, std::size_t to) {
  Range range;
  range.blocks = scanBlocksWithLastLine(source);
  from = std::min(from, source.size());
  to = std::min(to, source.size());
  if(from > to) std::swap(from, to);
  range.first = blockIndexAt(range.blocks, from);
  range.last = blockIndexAt(range.blocks, to);
  if(range.last < range.first) std::swap(range.first, range.last);
  range.start = range.blocks[range.first].start;
  range.end = range.blocks[range.last].end;
  range.valid = range.end > range.start;
  return range;
}

}

std::string blockMarker(BlockKind kind, int level, int listDepth, int ordinal, bool checked) {
  const std::string pad(static_cast<std::size_t>(std::max(0, listDepth)) * 2, ' ');
  switch(kind) {
    case BlockKind::Heading:
      return std::string(static_cast<std::size_t>(std::clamp(level, 1, 6)), '#') + " ";
    case BlockKind::Bullet:
      return pad + "- ";
    case BlockKind::Todo:
      return pad + (checked ? "- [x] " : "- [ ] ");
    case BlockKind::Ordered:
      return pad + std::to_string(ordinal > 0 ? ordinal : 1) + ". ";
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

Edit turnInto(std::string_view source, std::size_t caret, BlockKind kind, int level) {
  Edit edit;
  const Context context = contextAt(source, caret);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind == BlockKind::Complex) return edit;
  // Rewriting a block as what it already is does nothing - except where the
  // kind carries a variant, as a heading carries its rank and a callout its
  // alert. Comparing the markers covers both without a case for each.
  if(block.kind == kind && kind != BlockKind::Heading && kind != BlockKind::Callout) return edit;
  if(block.kind == kind &&
     source.substr(block.start, block.contentStart - block.start) ==
         blockMarker(kind, level, block.listDepth, block.ordinal, block.checked)) {
    return edit;
  }

  if(kind == BlockKind::Divider) {
    edit.valid = true;
    edit.start = block.start;
    edit.end = block.end;
    edit.text = "---\n";
    edit.cursor = block.start + edit.text.size();
    return edit;
  }

  // The content a fence wraps is the block body; every other kind keeps its
  // payload where the scanner marked it.
  const std::size_t contentStart = block.contentStart;
  const std::size_t contentEnd = std::max(contentStart, block.contentEnd);
  std::string content(source.substr(contentStart, contentEnd - contentStart));

  if(kind == BlockKind::Code) {
    edit.valid = true;
    edit.start = block.start;
    edit.end = block.end;
    std::string body = content;
    if(!body.empty() && body.back() != '\n') body.push_back('\n');
    edit.text = "```\n" + body + "```\n";
    edit.cursor = block.start + 4 + (caret > contentStart ? std::min(caret - contentStart, content.size()) : 0);
    return edit;
  }

  const int depth = isListKind(kind) && isListKind(block.kind) ? block.listDepth : 0;
  const int ordinal = kind == BlockKind::Ordered ? block.ordinal : 0;
  const bool checked = kind == BlockKind::Todo && block.kind == BlockKind::Todo && block.checked;
  const std::string marker = blockMarker(kind, level, depth, ordinal, checked);

  edit.valid = true;
  if(block.kind == BlockKind::Code) {
    // Un-fencing: the body survives verbatim, the new marker leads its first line.
    edit.start = block.start;
    edit.end = block.end;
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

Edit toggleTodo(std::string_view source, std::size_t caret) {
  Edit edit;
  const Context context = contextAt(source, caret);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind == BlockKind::Todo) {
    // Walk back from the content to the "[" the scanner already validated.
    std::size_t state = block.contentStart;
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
    edit.start = block.contentStart;
    edit.end = block.contentStart;
    edit.text = "[ ] ";
    edit.cursor = caret >= block.contentStart ? caret + edit.text.size() : caret;
    return edit;
  }
  return turnInto(source, caret, BlockKind::Todo);
}

Edit indent(std::string_view source, std::size_t caret) {
  Edit edit;
  const Context context = contextAt(source, caret);
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

Edit outdent(std::string_view source, std::size_t caret) {
  Edit edit;
  const Context context = contextAt(source, caret);
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

// Every move is the same operation: lift the blocks [first, last] together with
// the blank run that separates them from their neighbour, then drop the pair at
// `dest`. The separator travels on the group's trailing side going up and its
// leading side going down, which is what stops two paragraphs from merging into
// one when they change places.
Edit moveGroup(std::string_view source, const std::vector<SourceBlock>& blocks, std::size_t first,
               std::size_t last, std::size_t dest, std::size_t caret, bool selects) {
  Edit edit;
  const std::size_t gs = blocks[first].start;
  const std::size_t ge = blocks[last].end;
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

}

Edit moveBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret, int delta) {
  Edit edit;
  if(delta == 0) return edit;
  const Range range = rangeAt(source, fromCaret, toCaret);
  if(!range.valid) return edit;
  const auto& blocks = range.blocks;
  if(range.first == range.last && blocks[range.first].kind == BlockKind::Blank) return edit;

  std::size_t destination = 0;
  if(delta < 0) {
    std::size_t j = range.first;
    while(j > 0 && blocks[j - 1].kind == BlockKind::Blank) --j;
    if(j == 0) return edit;
    destination = blocks[j - 1].start;
  } else {
    std::size_t j = range.last;
    while(j + 1 < blocks.size() && blocks[j + 1].kind == BlockKind::Blank) ++j;
    if(j + 1 >= blocks.size()) return edit;
    destination = blocks[j + 1].end;
  }
  return moveGroup(source, blocks, range.first, range.last, destination, std::min(fromCaret, toCaret),
                   fromCaret != toCaret);
}

Edit moveBlock(std::string_view source, std::size_t caret, int delta) {
  return moveBlocks(source, caret, caret, delta);
}

Edit duplicateBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret) {
  Edit edit;
  const Range range = rangeAt(source, fromCaret, toCaret);
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

Edit duplicateBlock(std::string_view source, std::size_t caret) {
  return duplicateBlocks(source, caret, caret);
}

Edit deleteBlocks(std::string_view source, std::size_t fromCaret, std::size_t toCaret) {
  Edit edit;
  const Range range = rangeAt(source, fromCaret, toCaret);
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

Edit deleteBlock(std::string_view source, std::size_t caret) {
  return deleteBlocks(source, caret, caret);
}

Edit turnBlocksInto(std::string_view source, std::size_t fromCaret, std::size_t toCaret,
                    BlockKind kind, int level) {
  Edit edit;
  const Range range = rangeAt(source, fromCaret, toCaret);
  // The empty last line spans no bytes, but it is still somewhere a block can
  // be started - which is exactly what the slash menu does there.
  if(!range.valid) return turnInto(source, fromCaret, kind, level);

  // Work on the span alone, back to front, so each rewrite leaves the offsets
  // ahead of it untouched and the rescan `turnInto` does stays proportional to
  // the selection rather than to the note.
  std::string chunk(source.substr(range.start, range.end - range.start));
  const auto blocks = scanBlocks(chunk);
  bool changed = false;
  for(std::size_t i = blocks.size(); i-- > 0;) {
    if(blocks[i].kind == BlockKind::Blank) continue;
    const Edit one = turnInto(chunk, blocks[i].contentStart, kind, level);
    if(!one.valid) continue;
    chunk.replace(one.start, one.end - one.start, one.text);
    changed = true;
  }
  // A range holding nothing but blank lines has no block to rewrite - but the
  // caret still sits somewhere a block can be started.
  if(!changed) return turnInto(source, fromCaret, kind, level);

  edit.valid = true;
  edit.start = range.start;
  edit.end = range.end;
  edit.text = chunk;
  edit.anchor = range.start;
  edit.cursor = range.start + chunk.size();
  edit.selects = true;
  return edit;
}

Edit insertBlockAfter(std::string_view source, std::size_t caret, BlockKind kind, int level) {
  Edit edit;
  const Range range = rangeAt(source, caret, caret);
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
                  std::size_t destination) {
  const Range range = rangeAt(source, fromCaret, toCaret);
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

Edit continueList(std::string_view source, std::size_t caret) {
  Edit edit;
  const Context context = contextAt(source, caret);
  const SourceBlock& block = context.blocks[context.index];
  const bool list = isListKind(block.kind);
  const bool quote = block.kind == BlockKind::Quote || block.kind == BlockKind::Callout;
  if(!list && !quote) return edit;
  if(caret < block.contentStart) return edit;

  if(block.contentEnd <= block.contentStart) {
    // Enter on an empty item leaves the list instead of adding another.
    if(list && block.listDepth > 0) return outdent(source, caret);
    // Dropping the marker alone is not enough to get out: "- one\ntext" and
    // "> a\ntext" are lazy continuations, so what the file says would still be
    // one list item or one quote, whatever the screen showed. The blank line is
    // what actually ends the block.
    edit.valid = true;
    edit.start = block.start;
    edit.end = block.contentStart;
    edit.text = "\n";
    edit.cursor = block.start + 1;
    return edit;
  }

  edit.valid = true;
  edit.start = caret;
  edit.end = caret;
  edit.text = "\n" + (list ? blockMarker(block.kind, 0, block.listDepth, block.ordinal + 1, false)
                           : std::string("> "));
  edit.cursor = caret + edit.text.size();
  return edit;
}

Edit closeFence(std::string_view source, std::size_t caret) {
  Edit edit;
  const Context context = contextAt(source, caret);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind != BlockKind::Code) return edit;
  if(block.contentEnd != block.end) return edit;  // a closing fence is already there
  if(caret > lineEndFrom(source, block.start)) return edit;

  std::size_t i = block.start;
  while(i < source.size() && (source[i] == ' ' || source[i] == '\t')) ++i;
  const char marker = i < source.size() ? source[i] : '`';
  if(marker != '`' && marker != '~') return edit;
  std::size_t length = 0;
  while(i + length < source.size() && source[i + length] == marker) ++length;

  edit.valid = true;
  edit.start = caret;
  edit.end = caret;
  edit.text = "\n\n" + std::string(length, marker);
  edit.cursor = caret + 1;
  return edit;
}

Edit outdentOrUnwrap(std::string_view source, std::size_t caret) {
  Edit edit;
  const Context context = contextAt(source, caret);
  const SourceBlock& block = context.blocks[context.index];
  if(caret != block.contentStart || block.contentStart <= block.start) return edit;
  switch(block.kind) {
    case BlockKind::Complex:
    case BlockKind::Code:
    case BlockKind::Divider:
    case BlockKind::Blank:
      return edit;
    default:
      break;
  }
  if(isListKind(block.kind) && block.listDepth > 0) return outdent(source, caret);
  edit.valid = true;
  edit.start = block.start;
  edit.end = block.contentStart;
  edit.cursor = block.start;
  return edit;
}

Edit applyMarkdownShortcut(std::string_view source, std::size_t caret) {
  Edit edit;
  // This runs on every typed space, so reject on the two bytes every shortcut
  // ends with before paying for a scan of the buffer.
  if(caret < 3 || caret > source.size() || source[caret - 1] != ' ' || source[caret - 2] != ']') return edit;
  const Context context = contextAt(source, caret);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind == BlockKind::Code || block.kind == BlockKind::Complex) return edit;
  const std::size_t from = block.contentStart;
  if(caret <= from || caret > source.size()) return edit;

  const std::string_view typed = source.substr(from, caret - from);
  if(typed != "[] " && typed != "[ ] " && typed != "[x] " && typed != "[X] ") return edit;
  const bool checked = typed == "[x] " || typed == "[X] ";

  // A bullet only needs the checkbox normalised; a paragraph needs the whole
  // task marker. Every other Markdown shortcut is already what the user typed.
  std::string replacement;
  if(block.kind == BlockKind::Bullet) replacement = checked ? "[x] " : "[ ] ";
  else if(block.kind == BlockKind::Paragraph) replacement = checked ? "- [x] " : "- [ ] ";
  else return edit;

  edit.valid = true;
  edit.start = from;
  edit.end = caret;
  edit.text = replacement;
  edit.cursor = from + replacement.size();
  return edit;
}

}
