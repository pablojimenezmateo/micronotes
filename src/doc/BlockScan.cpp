#include "doc/BlockScan.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace micronotes::doc {
namespace {

struct Line {
  std::size_t start = 0;
  std::size_t end = 0;   // excludes the newline
  std::size_t next = 0;  // start of the following line, or source.size()
};

Line lineAt(std::string_view source, std::size_t start) {
  Line line;
  line.start = start;
  const auto newline = source.find('\n', start);
  if(newline == std::string_view::npos) {
    line.end = source.size();
    line.next = source.size();
  } else {
    line.end = newline;
    line.next = newline + 1;
  }
  return line;
}

std::string_view textOf(std::string_view source, const Line& line) {
  return source.substr(line.start, line.end - line.start);
}

bool isBlank(std::string_view text) {
  return text.find_first_not_of(" \t\r") == std::string_view::npos;
}

// Leading whitespace measured in columns, with tabs worth four.
std::size_t indentColumns(std::string_view text, std::size_t* bytes = nullptr) {
  std::size_t columns = 0;
  std::size_t i = 0;
  for(; i < text.size(); ++i) {
    if(text[i] == ' ') ++columns;
    else if(text[i] == '\t') columns += 4;
    else break;
  }
  if(bytes) *bytes = i;
  return columns;
}

int depthFromIndent(std::size_t columns) {
  return static_cast<int>(std::min<std::size_t>(columns / 2, 8));
}

// A run of three or more of the same rule character, spaces allowed between.
bool isDivider(std::string_view body) {
  if(body.empty()) return false;
  const char rule = body[0];
  if(rule != '-' && rule != '*' && rule != '_') return false;
  int count = 0;
  for(const char c : body) {
    if(c == rule) ++count;
    else if(c != ' ' && c != '\t' && c != '\r') return false;
  }
  return count >= 3;
}

struct Fence {
  bool matched = false;
  char marker = '`';
  std::size_t length = 0;
  std::size_t infoStart = 0;
};

Fence matchFence(std::string_view body, std::size_t bodyOffset) {
  Fence fence;
  if(body.empty()) return fence;
  const char marker = body[0];
  if(marker != '`' && marker != '~') return fence;
  std::size_t length = 0;
  while(length < body.size() && body[length] == marker) ++length;
  if(length < 3) return fence;
  fence.matched = true;
  fence.marker = marker;
  fence.length = length;
  fence.infoStart = bodyOffset + length;
  return fence;
}

struct Marker {
  bool matched = false;
  std::size_t contentStart = 0;  // absolute
  int ordinal = 0;
  bool ordered = false;
  bool todo = false;
  bool checked = false;
};

// "- ", "* ", "+ ", "1. ", "1) ", optionally followed by "[ ] " / "[x] ".
Marker matchListMarker(std::string_view source, const Line& line, std::size_t bodyStart) {
  Marker marker;
  std::string_view body = source.substr(bodyStart, line.end - bodyStart);
  std::size_t used = 0;
  if(!body.empty() && (body[0] == '-' || body[0] == '*' || body[0] == '+')) {
    used = 1;
  } else {
    std::size_t digits = 0;
    while(digits < body.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(body[digits]))) ++digits;
    if(digits == 0 || digits >= body.size()) return marker;
    if(body[digits] != '.' && body[digits] != ')') return marker;
    marker.ordered = true;
    marker.ordinal = std::atoi(std::string(body.substr(0, digits)).c_str());
    used = digits + 1;
  }
  // A marker must be followed by a space, or stand alone as an empty item.
  if(used < body.size() && body[used] != ' ' && body[used] != '\t') return marker;
  marker.matched = true;
  while(used < body.size() && (body[used] == ' ' || body[used] == '\t')) ++used;
  if(!marker.ordered && used + 2 < body.size() && body[used] == '[' && body[used + 2] == ']') {
    const char state = body[used + 1];
    if(state == ' ' || state == 'x' || state == 'X') {
      marker.todo = true;
      marker.checked = state != ' ';
      used += 3;
      while(used < body.size() && (body[used] == ' ' || body[used] == '\t')) ++used;
    }
  }
  marker.contentStart = bodyStart + used;
  return marker;
}

bool isTableDelimiterRow(std::string_view body) {
  bool sawDash = false;
  for(const char c : body) {
    if(c == '-') sawDash = true;
    else if(c != '|' && c != ':' && c != ' ' && c != '\t' && c != '\r') return false;
  }
  return sawDash && body.find('|') != std::string_view::npos;
}

bool isFootnoteDefinition(std::string_view body) {
  if(body.size() < 5 || body[0] != '[' || body[1] != '^') return false;
  const auto close = body.find("]:");
  return close != std::string_view::npos && close > 2;
}

// Would this line start a new block? Used to decide where a paragraph stops.
bool startsBlock(std::string_view source, const Line& line) {
  const std::string_view text = textOf(source, line);
  if(isBlank(text)) return true;
  std::size_t indentBytes = 0;
  const std::size_t columns = indentColumns(text, &indentBytes);
  const std::string_view body = text.substr(indentBytes);
  if(body.empty()) return true;
  const Line probe {line.start + indentBytes, line.end, line.next};
  // Checked before the indentation rule, because the scan loop resolves the
  // same ambiguity the same way: a list marker four columns in is a deeply
  // nested item far more often than it is indented code. If this disagreed,
  // whatever came above would swallow that item.
  if(matchListMarker(source, probe, line.start + indentBytes).matched) return true;
  if(columns >= 4) return false;
  if(body[0] == '#') {
    std::size_t hashes = 0;
    while(hashes < body.size() && body[hashes] == '#') ++hashes;
    if(hashes <= 6 && (hashes == body.size() || body[hashes] == ' ')) return true;
  }
  if(body[0] == '>') return true;
  if(body[0] == '<') return true;
  if(isDivider(body)) return true;
  if(matchFence(body, 0).matched) return true;
  if(isFootnoteDefinition(body)) return true;
  return false;
}

}

bool isListKind(BlockKind kind) {
  return kind == BlockKind::Bullet || kind == BlockKind::Ordered || kind == BlockKind::Todo;
}

std::vector<SourceBlock> scanBlocks(std::string_view source) {
  std::vector<SourceBlock> blocks;
  // Roughly one block per two lines of typical prose; a good enough guess to
  // keep a full rescan from reallocating on every keystroke.
  blocks.reserve(source.size() / 48 + 8);
  std::size_t pos = 0;
  while(pos < source.size()) {
    const Line line = lineAt(source, pos);
    const std::string_view text = textOf(source, line);
    std::size_t indentBytes = 0;
    const std::size_t columns = indentColumns(text, &indentBytes);
    const std::size_t bodyStart = line.start + indentBytes;
    const std::string_view body = text.substr(indentBytes);

    SourceBlock block;
    block.start = line.start;
    block.end = line.next;
    block.contentStart = bodyStart;
    block.contentEnd = line.end;
    block.listDepth = depthFromIndent(columns);

    if(isBlank(text)) {
      block.kind = BlockKind::Blank;
      block.contentStart = line.start;
      block.contentEnd = line.start;
      blocks.push_back(std::move(block));
      pos = line.next;
      continue;
    }

    // Four columns of indentation open a Markdown code block. A list marker at
    // that depth is far more likely to be a deeply nested item, so it wins.
    const Marker leadingMarker = matchListMarker(source, line, bodyStart);
    if(columns >= 4 && !leadingMarker.matched) {
      block.kind = BlockKind::Complex;
      std::size_t scan = line.next;
      std::size_t lastContentEnd = line.next;
      while(scan < source.size()) {
        const Line inner = lineAt(source, scan);
        const std::string_view innerText = textOf(source, inner);
        if(!isBlank(innerText) && indentColumns(innerText) < 4) break;
        scan = inner.next;
        if(!isBlank(innerText)) lastContentEnd = scan;
      }
      block.end = lastContentEnd;
      block.contentStart = line.start;
      block.contentEnd = lastContentEnd > line.start && source[lastContentEnd - 1] == '\n' ? lastContentEnd - 1 : lastContentEnd;
      blocks.push_back(std::move(block));
      pos = lastContentEnd;
      continue;
    }

    // Fenced code owns everything up to its closing fence, so a `#` inside a
    // fence is never mistaken for a heading.
    if(const Fence fence = matchFence(body, bodyStart); fence.matched && columns < 4) {
      block.kind = BlockKind::Code;
      std::string_view info = source.substr(fence.infoStart, line.end - fence.infoStart);
      while(!info.empty() && (info.front() == ' ' || info.front() == '\t')) info.remove_prefix(1);
      while(!info.empty() && (info.back() == ' ' || info.back() == '\t' || info.back() == '\r')) info.remove_suffix(1);
      block.info = std::string(info);
      block.contentStart = line.next;
      std::size_t scan = line.next;
      std::size_t contentEnd = line.next;
      std::size_t blockEnd = line.next;
      while(scan < source.size()) {
        const Line inner = lineAt(source, scan);
        const std::string_view innerText = textOf(source, inner);
        std::size_t innerIndentBytes = 0;
        indentColumns(innerText, &innerIndentBytes);
        const std::string_view innerBody = innerText.substr(innerIndentBytes);
        const Fence closing = matchFence(innerBody, 0);
        if(closing.matched && closing.marker == fence.marker && closing.length >= fence.length) {
          contentEnd = inner.start;
          blockEnd = inner.next;
          scan = inner.next;
          break;
        }
        scan = inner.next;
        contentEnd = scan;
        blockEnd = scan;
      }
      block.contentEnd = std::max(block.contentStart, contentEnd);
      block.end = blockEnd;
      blocks.push_back(std::move(block));
      pos = blockEnd;
      continue;
    }

    // Tables, raw HTML and footnote definitions are handed to md4c whole.
    bool complex = false;
    if(columns < 4) {
      if(body.find('|') != std::string_view::npos && line.next < source.size()) {
        const Line following = lineAt(source, line.next);
        std::size_t followingIndent = 0;
        const std::string_view followingText = textOf(source, following);
        indentColumns(followingText, &followingIndent);
        if(isTableDelimiterRow(followingText.substr(followingIndent))) complex = true;
      }
      if(!complex && body[0] == '<') complex = true;
      if(!complex && isFootnoteDefinition(body)) complex = true;
    }
    if(complex) {
      block.kind = BlockKind::Complex;
      std::size_t scan = line.next;
      while(scan < source.size()) {
        const Line inner = lineAt(source, scan);
        if(isBlank(textOf(source, inner))) break;
        scan = inner.next;
      }
      block.end = scan;
      block.contentStart = line.start;
      block.contentEnd = scan > line.start && source[scan - 1] == '\n' ? scan - 1 : scan;
      blocks.push_back(std::move(block));
      pos = scan;
      continue;
    }

    if(columns < 4 && isDivider(body)) {
      block.kind = BlockKind::Divider;
      // The whole line is marker; there is nothing to type into.
      block.contentStart = line.end;
      block.contentEnd = line.end;
      blocks.push_back(std::move(block));
      pos = line.next;
      continue;
    }

    if(columns < 4 && !body.empty() && body[0] == '#') {
      std::size_t hashes = 0;
      while(hashes < body.size() && body[hashes] == '#') ++hashes;
      if(hashes <= 6 && (hashes == body.size() || body[hashes] == ' ')) {
        block.kind = BlockKind::Heading;
        block.level = static_cast<int>(hashes);
        std::size_t after = hashes;
        while(after < body.size() && body[after] == ' ') ++after;
        block.contentStart = bodyStart + after;
        blocks.push_back(std::move(block));
        pos = line.next;
        continue;
      }
    }

    if(columns < 4 && !body.empty() && body[0] == '>') {
      std::size_t after = 1;
      while(after < body.size() && (body[after] == ' ' || body[after] == '\t')) ++after;
      block.kind = BlockKind::Quote;
      block.contentStart = bodyStart + after;
      const std::string_view rest = body.substr(after);
      if(rest.size() > 3 && rest[0] == '[' && rest[1] == '!') {
        const auto close = rest.find(']');
        if(close != std::string_view::npos) {
          block.kind = BlockKind::Callout;
          block.info = std::string(rest.substr(2, close - 2));
          std::size_t contentAfter = after + close + 1;
          while(contentAfter < body.size() && body[contentAfter] == ' ') ++contentAfter;
          block.contentStart = bodyStart + contentAfter;
        }
      }
      blocks.push_back(std::move(block));
      pos = line.next;
      continue;
    }

    if(const Marker& marker = leadingMarker; marker.matched) {
      block.kind = marker.todo ? BlockKind::Todo : (marker.ordered ? BlockKind::Ordered : BlockKind::Bullet);
      block.ordered = marker.ordered;
      block.ordinal = marker.ordinal;
      block.checked = marker.checked;
      block.contentStart = std::min(marker.contentStart, line.end);
      // An item owns the lines that continue its text, on the same terms as a
      // paragraph: everything up to a blank line or to something that starts a
      // block of its own, which a nested item does. Without this a hand-wrapped
      // item is several blocks, and the surface shows a break and the
      // continuation's indent where the file means one flowing item.
      std::size_t scan = line.next;
      std::size_t contentEnd = line.end;
      while(scan < source.size()) {
        const Line inner = lineAt(source, scan);
        if(startsBlock(source, inner)) break;
        contentEnd = inner.end;
        scan = inner.next;
      }
      block.contentEnd = contentEnd;
      block.end = scan;
      blocks.push_back(std::move(block));
      pos = scan;
      continue;
    }

    // A paragraph absorbs following lines until something else starts.
    block.kind = BlockKind::Paragraph;
    block.contentStart = line.start;
    std::size_t scan = line.next;
    std::size_t contentEnd = line.end;
    while(scan < source.size()) {
      const Line inner = lineAt(source, scan);
      if(startsBlock(source, inner)) break;
      contentEnd = inner.end;
      scan = inner.next;
    }
    block.contentEnd = contentEnd;
    block.end = scan;
    blocks.push_back(std::move(block));
    pos = scan;
  }

  if(blocks.empty()) {
    SourceBlock block;
    block.kind = BlockKind::Paragraph;
    blocks.push_back(block);
  }
  return blocks;
}

bool startsQuoteRun(const std::vector<SourceBlock>& blocks, std::size_t index) {
  if(index >= blocks.size()) return false;
  const BlockKind kind = blocks[index].kind;
  if(kind == BlockKind::Callout) return true;
  if(kind != BlockKind::Quote) return false;
  if(index == 0) return true;
  const BlockKind previous = blocks[index - 1].kind;
  return previous != BlockKind::Quote && previous != BlockKind::Callout;
}

bool endsQuoteRun(const std::vector<SourceBlock>& blocks, std::size_t index) {
  if(index >= blocks.size()) return false;
  const BlockKind kind = blocks[index].kind;
  if(kind != BlockKind::Quote && kind != BlockKind::Callout) return false;
  return index + 1 >= blocks.size() || blocks[index + 1].kind != BlockKind::Quote;
}

std::size_t blockIndexAt(const std::vector<SourceBlock>& blocks, std::size_t offset) {
  if(blocks.empty()) return 0;
  std::size_t low = 0;
  std::size_t high = blocks.size();
  while(low + 1 < high) {
    const std::size_t mid = (low + high) / 2;
    if(blocks[mid].start <= offset) low = mid;
    else high = mid;
  }
  return low;
}

}
