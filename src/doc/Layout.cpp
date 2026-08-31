#include "doc/Layout.h"

#include "doc/Fold.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace micronotes::doc {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// FNV-1a widened to a machine word: a block's fingerprint is recomputed for
// every block on every keystroke, so the byte-at-a-time loop is worth avoiding.
std::uint64_t hashBytes(std::uint64_t seed, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t i = 0;
  for(; i + sizeof(std::uint64_t) <= size; i += sizeof(std::uint64_t)) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes + i, sizeof(word));
    seed = (seed ^ word) * kFnvPrime;
    seed ^= seed >> 29;
  }
  for(; i < size; ++i) {
    seed ^= bytes[i];
    seed *= kFnvPrime;
  }
  return seed;
}

template <typename T>
std::uint64_t hashValue(std::uint64_t seed, const T& value) {
  return hashBytes(seed, &value, sizeof(T));
}

std::size_t utf8Next(std::string_view text, std::size_t index) {
  if(index >= text.size()) return text.size();
  std::size_t next = index + 1;
  while(next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
  return next;
}

bool isSpaceByte(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Newlines and tabs become one space each, so a run's text stays byte-aligned
// with the source it came from and prefix measurement maps offsets to pixels.
std::string displayText(std::string_view source) {
  std::string out(source);
  for(char& c : out) {
    if(c == '\n' || c == '\t' || c == '\r') c = ' ';
  }
  return out;
}

struct Attr {
  bool strong = false;
  bool italic = false;
  bool mono = false;
  bool strike = false;
  bool marker = false;
  int link = -1;
  TextRole role = TextRole::Body;

  bool operator==(const Attr& other) const {
    return strong == other.strong && italic == other.italic && mono == other.mono &&
           strike == other.strike && marker == other.marker && link == other.link &&
           role == other.role;
  }
};

struct Token {
  std::size_t start = 0;
  std::size_t end = 0;
  std::string text;
  RunStyle style;
  TextRole role = TextRole::Body;
  bool isMarker = false;
  bool hidden = false;
  bool space = false;
  int link = -1;
};

using LineGroup = std::vector<Token>;

Token makeToken(std::string_view source, std::size_t start, std::size_t end, const RunStyle& style, TextRole role, bool marker, bool hidden, int link) {
  Token token;
  token.start = start;
  token.end = end;
  token.style = style;
  token.role = role;
  token.isMarker = marker;
  token.hidden = hidden;
  token.link = link;
  if(!hidden) token.text = displayText(source.substr(start, end - start));
  token.space = !token.text.empty() && std::all_of(token.text.begin(), token.text.end(), [](char c) { return c == ' '; });
  return token;
}

class Flow {
public:
  Flow(const Metrics& metrics, std::size_t base, float textLeft, float width, float lineHeight, bool wrap, float top, BlockLayout& out)
      : metrics_(metrics), base_(base), textLeft_(textLeft), right_(textLeft + width), lineHeight_(lineHeight), wrap_(wrap), y_(top), out_(out) {
    penX_ = textLeft_;
  }

  void run(const std::vector<LineGroup>& groups) {
    for(const auto& group : groups) {
      for(const auto& token : group) {
        if(token.hidden || token.text.empty()) {
          // Flush first: a zero-width run must sit after the spaces that
          // precede it, or the offset it anchors lands inside them.
          flushPending();
          emit(token, 0.0f);
          continue;
        }
        if(token.space) {
          pending_.push_back(token);
          continue;
        }
        const float width = metrics_.measure(token.text, token.style);
        float pendingWidth = 0.0f;
        for(const auto& space : pending_) pendingWidth += metrics_.measure(space.text, space.style);
        if(wrap_ && penX_ > textLeft_ && penX_ + pendingWidth + width > right_) {
          flushPending();
          pushLine();
        } else {
          flushPending();
        }
        if(wrap_ && width > right_ - textLeft_ && penX_ <= textLeft_) {
          splitWord(token);
          continue;
        }
        emit(token, width);
      }
      flushPending();
      pushLine();
    }
  }

  float bottom() const {
    return y_;
  }

private:
  void emit(const Token& token, float width) {
    TextRun run;
    run.srcStart = token.start - base_;
    run.srcEnd = token.end - base_;
    run.rect = {penX_, 0.0f, width, lineHeight_};
    run.style = token.style;
    run.role = token.role;
    run.isMarker = token.isMarker;
    run.linkIndex = token.link;
    run.text = token.hidden ? std::string() : token.text;
    runs_.push_back(std::move(run));
    penX_ += width;
  }

  void flushPending() {
    for(const auto& space : pending_) emit(space, metrics_.measure(space.text, space.style));
    pending_.clear();
  }

  void pushLine() {
    VisualLine line;
    line.y = y_;
    line.height = lineHeight_;
    line.runs = std::move(runs_);
    runs_.clear();
    out_.lines.push_back(std::move(line));
    y_ += lineHeight_;
    penX_ = textLeft_;
  }

  // A word wider than the whole column is broken at codepoint boundaries so it
  // never disappears past the right edge.
  void splitWord(const Token& token) {
    std::size_t i = 0;
    while(i < token.text.size()) {
      std::size_t j = i;
      float accumulated = 0.0f;
      while(j < token.text.size()) {
        const std::size_t next = utf8Next(token.text, j);
        const float width = metrics_.measure(token.text.substr(j, next - j), token.style);
        if(j > i && penX_ + accumulated + width > right_) break;
        accumulated += width;
        j = next;
      }
      Token piece = token;
      piece.start = token.start + i;
      piece.end = token.start + j;
      piece.text = token.text.substr(i, j - i);
      emit(piece, accumulated);
      i = j;
      if(i < token.text.size()) pushLine();
    }
  }

  const Metrics& metrics_;
  std::size_t base_ = 0;
  float textLeft_ = 0.0f;
  float right_ = 0.0f;
  float lineHeight_ = 0.0f;
  bool wrap_ = true;
  float y_ = 0.0f;
  float penX_ = 0.0f;
  std::vector<TextRun> runs_;
  std::vector<Token> pending_;
  BlockLayout& out_;
};

RunStyle styleFrom(const RunStyle& base, const Attr& attr, float monoSize) {
  RunStyle style = base;
  if(attr.strong) style.strong = true;
  if(attr.italic) style.italic = true;
  if(attr.strike) style.strike = true;
  if(attr.mono) {
    style.mono = true;
    style.size = monoSize;
  }
  return style;
}

// Splits `[from, to)` into tokens that share one set of inline attributes, with
// whitespace kept as its own token so wrapping has break opportunities.
void appendContentTokens(std::string_view source, std::size_t from, std::size_t to, const std::vector<Attr>& attrs,
                         const RunStyle& base, float monoSize, bool revealed, LineGroup& out) {
  std::size_t i = from;
  while(i < to) {
    const Attr& attr = attrs[i - from];
    const bool space = isSpaceByte(source[i]);
    std::size_t j = i + 1;
    while(j < to && attrs[j - from] == attr && isSpaceByte(source[j]) == space) ++j;
    const bool hidden = attr.marker && !revealed;
    const RunStyle style = styleFrom(base, attr, monoSize);
    const TextRole role = attr.marker ? TextRole::Marker : attr.role;
    // A line ending inside a block is one space, however many bytes it took:
    // the newline itself, plus the indentation of the line continuing it. All
    // but the last byte are emitted hidden, which is zero width and still
    // addressable, so the source stays byte-aligned with what is drawn and a
    // hand-wrapped sentence reads as the one space the file means.
    const bool foldsLineEnding = space && !hidden && j - i > 1 &&
                                 source.substr(i, j - i).find('\n') != std::string_view::npos;
    if(foldsLineEnding) {
      out.push_back(makeToken(source, i, j - 1, style, role, attr.marker, true, -1));
      out.push_back(makeToken(source, j - 1, j, style, role, attr.marker, false, attr.marker ? -1 : attr.link));
      i = j;
      continue;
    }
    out.push_back(makeToken(source, i, j, style, role, attr.marker, hidden, attr.marker ? -1 : attr.link));
    i = j;
  }
}

std::vector<std::pair<std::size_t, std::size_t>> sourceLines(std::string_view source, std::size_t from, std::size_t to) {
  std::vector<std::pair<std::size_t, std::size_t>> lines;
  std::size_t start = from;
  while(start < to) {
    auto newline = source.find('\n', start);
    if(newline == std::string_view::npos || newline >= to) newline = to;
    lines.push_back({start, newline});
    start = newline < to ? newline + 1 : to;
    if(newline == to) break;
  }
  if(lines.empty()) lines.push_back({from, to});
  return lines;
}

}

void DocumentLayout::setMetrics(Metrics metrics) {
  metrics_ = std::move(metrics);
  cache_.clear();
}

const std::vector<SourceBlock>& DocumentLayout::blocks() const {
  return blocks_;
}

std::size_t DocumentLayout::blockCount() const {
  return blocks_.size();
}

const BlockLayout& DocumentLayout::layout(std::size_t index) const {
  static const BlockLayout empty;
  if(index >= placed_.size() || !placed_[index].layout) return empty;
  return *placed_[index].layout;
}

bool DocumentLayout::blockHidden(std::size_t index) const {
  return index < hidden_.size() && hidden_[index];
}

float DocumentLayout::blockTop(std::size_t index) const {
  if(index >= placed_.size()) return totalHeight_;
  return placed_[index].top;
}

float DocumentLayout::totalHeight() const {
  return totalHeight_;
}

const LayoutOptions& DocumentLayout::options() const {
  return options_;
}

const std::string& DocumentLayout::source() const {
  return source_;
}

std::size_t DocumentLayout::cachedBlockCount() const {
  return cache_.size();
}

std::size_t DocumentLayout::lastRelaidBlocks() const {
  return lastRelaid_;
}

void DocumentLayout::update(std::string_view source, const LayoutOptions& options) {
  source_.assign(source);
  options_ = options;
  blocks_ = scanBlocks(source_);
  const std::string_view text = source_;

  std::uint64_t geometry = kFnvOffset;
  geometry = hashValue(geometry, options.width);
  geometry = hashValue(geometry, options.fontScale);
  geometry = hashValue(geometry, options.indentStep);
  geometry = hashValue(geometry, options.listGutter);
  geometry = hashValue(geometry, options.quoteGutter);
  geometry = hashValue(geometry, options.blockSpacing);
  geometry = hashValue(geometry, options.headingSpaceAbove);
  geometry = hashBytes(geometry, &options.type, sizeof(options.type));

  // Fold ranges come from the block structure, so they can only be resolved
  // once the scan is in: the caller names the heads, the layout names the
  // blocks each head swallows.
  hidden_.assign(blocks_.size(), false);
  for(std::size_t i = 0; options.folded && i < blocks_.size(); ++i) {
    // A fold nested inside a collapsed one is already hidden, and costs
    // nothing to resolve again.
    if(hidden_[i] || !foldableKind(blocks_[i].kind) || !options.folded(blocks_[i])) continue;
    const std::size_t end = foldEnd(blocks_, i);
    for(std::size_t j = i + 1; j < end; ++j) hidden_[j] = true;
  }

  const std::size_t caretBlock = options.caretOffset == kNone ? kNone : blockIndexAt(blocks_, std::min(options.caretOffset, source_.size()));
  const std::size_t rawBlock = options.rawOffset == kNone ? kNone : blockIndexAt(blocks_, std::min(options.rawOffset, source_.size()));

  placed_.assign(blocks_.size(), Placed {});
  liveKeys_.clear();
  liveKeys_.reserve(blocks_.size());
  lastRelaid_ = 0;
  float top = 0.0f;
  for(std::size_t i = 0; i < blocks_.size(); ++i) {
    const SourceBlock& block = blocks_[i];
    Flags flags;
    flags.revealed = options.revealAll || i == caretBlock;
    flags.raw = i == rawBlock;
    // A buffer ending in a newline has one more (empty) line to put a caret on.
    flags.trailingLine = i + 1 == blocks_.size() && block.end == source_.size() &&
                         !source_.empty() && source_.back() == '\n';
    flags.hidden = hidden_[i];
    flags.groupFirst = startsQuoteRun(blocks_, i);
    flags.groupLast = endsQuoteRun(blocks_, i);
    const bool first = i == 0;
    std::uint64_t key = hashBytes(geometry, text.data() + block.start, block.end - block.start);
    key = hashValue(key, block.kind);
    key = hashValue(key, block.level);
    key = hashValue(key, block.listDepth);
    key = hashValue(key, block.ordinal);
    key = hashValue(key, block.checked);
    key = hashBytes(key, &flags, sizeof(flags));
    key = hashValue(key, first);

    auto found = cache_.find(key);
    if(found == cache_.end()) {
      ++lastRelaid_;
      found = cache_.emplace(key, layoutBlock(i, flags)).first;
    }
    placed_[i].blockIndex = i;
    placed_[i].top = top;
    placed_[i].layout = &found->second;
    liveKeys_.push_back(key);
    top += found->second.height;
  }
  totalHeight_ = top;

  flatLines_.clear();
  flatLines_.reserve(blocks_.size() + blocks_.size() / 2);
  for(std::size_t i = 0; i < placed_.size(); ++i) {
    const BlockLayout& layout = *placed_[i].layout;
    for(std::size_t line = 0; line < layout.lines.size(); ++line) {
      flatLines_.push_back({i, line, placed_[i].top + layout.lines[line].y});
    }
  }

  // Bounded memory: keep roughly one spare generation of block layouts.
  if(cache_.size() > blocks_.size() * 3 + 256) {
    std::vector<std::uint64_t> live = liveKeys_;
    std::sort(live.begin(), live.end());
    for(auto it = cache_.begin(); it != cache_.end();) {
      if(std::binary_search(live.begin(), live.end(), it->first)) ++it;
      else it = cache_.erase(it);
    }
    for(std::size_t i = 0; i < placed_.size(); ++i) {
      placed_[i].layout = &cache_.find(liveKeys_[i])->second;
    }
  }
}

BlockLayout DocumentLayout::layoutBlock(std::size_t index, const Flags& flags) const {
  const bool revealed = flags.revealed;
  const bool raw = flags.raw;
  const bool trailingLine = flags.trailingLine;
  const SourceBlock& block = blocks_[index];
  const std::string_view source = source_;
  const TypeMetrics& type = options_.type;

  BlockLayout out;
  out.kind = block.kind;
  out.revealed = revealed;
  out.raw = raw;

  RunStyle base;
  base.size = type.body;
  float padTop = 0.0f;
  float padBottom = options_.blockSpacing;
  out.indent = static_cast<float>(block.listDepth) * options_.indentStep;
  out.textLeft = out.indent;

  switch(block.kind) {
    case BlockKind::Heading:
      base.size = type.heading[std::clamp(block.level, 1, 6) - 1];
      base.strong = true;
      if(index > 0) padTop = options_.headingSpaceAbove;
      break;
    case BlockKind::Bullet:
    case BlockKind::Ordered:
    case BlockKind::Todo:
      out.textLeft = out.indent + options_.listGutter;
      break;
    case BlockKind::Quote:
    case BlockKind::Callout:
      out.textLeft = out.indent + options_.quoteGutter;
      // Padding belongs to the run, not to every line in it, or a three-line
      // callout would be drawn with three lots of air inside its own box.
      padTop = flags.groupFirst ? 8.0f : 0.0f;
      padBottom = flags.groupLast ? 8.0f : 0.0f;
      break;
    case BlockKind::Code:
      base.mono = true;
      base.size = type.mono;
      padTop = 8.0f;
      padBottom = 12.0f;
      break;
    case BlockKind::Divider:
      // A rule needs air on both sides or it reads as an underline.
      padTop = 10.0f;
      padBottom = 14.0f;
      break;
    case BlockKind::Blank:
      padBottom = 0.0f;
      break;
    default:
      break;
  }
  if(raw) {
    base = RunStyle {};
    base.mono = true;
    base.size = type.mono;
    out.textLeft = out.indent;
  }

  const float available = std::max(40.0f, options_.width - out.textLeft);
  const float lineHeight = metrics_.lineHeight ? metrics_.lineHeight(base) : base.size * type.lineHeightRatio;
  const auto appendTrailingLine = [&](float y) {
    VisualLine line;
    line.y = y;
    line.height = lineHeight;
    TextRun run;
    run.srcStart = block.end - block.start;
    run.srcEnd = run.srcStart;
    run.rect = {out.textLeft, 0.0f, 0.0f, lineHeight};
    run.style = base;
    line.runs.push_back(std::move(run));
    out.lines.push_back(std::move(line));
    return y + lineHeight;
  };

  // A collapsed block gives up its height and nothing else. The one thing it
  // may not give up is the empty last line: that is the only caret position at
  // the end of the buffer, and losing it would strand the caret.
  if(flags.hidden) {
    out.hidden = true;
    out.height = trailingLine ? appendTrailingLine(0.0f) : 0.0f;
    return out;
  }

  // A block the scanner does not model reserves the height md4c will need, and
  // exposes one caret position at its start until the user drops it to raw.
  if(block.kind == BlockKind::Complex && !raw) {
    out.complex = true;
    const float height = metrics_.measureComplex ? metrics_.measureComplex(block, options_.width) : lineHeight;
    VisualLine line;
    line.y = padTop;
    line.height = std::max(lineHeight, height);
    TextRun run;
    run.srcStart = 0;
    run.srcEnd = block.end - block.start;
    run.rect = {out.textLeft, 0.0f, 0.0f, line.height};
    run.style = base;
    run.role = TextRole::Body;
    line.runs.push_back(std::move(run));
    out.lines.push_back(std::move(line));
    float bottom = padTop + std::max(lineHeight, height);
    if(trailingLine) bottom = appendTrailingLine(bottom);
    out.height = bottom + padBottom;
    return out;
  }

  std::vector<LineGroup> groups;
  const RunStyle markerStyle = base;

  if(raw || block.kind == BlockKind::Code) {
    const bool fenced = block.kind == BlockKind::Code && !raw;
    const std::size_t from = fenced ? block.contentStart : block.start;
    const std::size_t to = fenced ? block.contentEnd : block.end;
    for(const auto& [lineStart, lineEnd] : sourceLines(source, from, to)) {
      LineGroup group;
      if(lineEnd > lineStart) {
        group.push_back(makeToken(source, lineStart, lineEnd, base, TextRole::Code, false, false, -1));
      }
      // The newline itself takes no space but must stay addressable.
      const std::size_t tail = std::min(lineEnd + 1, to);
      if(tail > lineEnd) group.push_back(makeToken(source, lineEnd, tail, base, TextRole::Code, false, true, -1));
      groups.push_back(std::move(group));
    }
    if(fenced) {
      LineGroup opening;
      opening.push_back(makeToken(source, block.start, block.contentStart, markerStyle, TextRole::Marker, true, !revealed, -1));
      if(revealed) {
        groups.insert(groups.begin(), std::move(opening));
      } else if(!groups.empty()) {
        groups.front().insert(groups.front().begin(), std::move(opening.front()));
      } else {
        groups.push_back(std::move(opening));
      }
      if(block.end > block.contentEnd) {
        Token closing = makeToken(source, block.contentEnd, block.end, markerStyle, TextRole::Marker, true, !revealed, -1);
        if(revealed) {
          LineGroup group;
          group.push_back(std::move(closing));
          groups.push_back(std::move(group));
        } else {
          groups.back().push_back(std::move(closing));
        }
      }
    }
  } else {
    LineGroup group;
    if(block.contentStart > block.start) {
      group.push_back(makeToken(source, block.start, block.contentStart, markerStyle, TextRole::Marker, true, !revealed, -1));
    }
    if(block.contentEnd > block.contentStart) {
      const std::size_t span = block.contentEnd - block.contentStart;
      std::vector<Attr> attrs(span);
      const auto inlines = scanInlines(source.substr(block.contentStart, span), block.contentStart);
      for(const auto& inlineSpan : inlines) {
        const auto apply = [&](std::size_t from, std::size_t to, const std::function<void(Attr&)>& fn) {
          for(std::size_t i = std::max(from, block.contentStart); i < std::min(to, block.contentEnd); ++i) {
            fn(attrs[i - block.contentStart]);
          }
        };
        apply(inlineSpan.openStart, inlineSpan.openEnd, [](Attr& a) { a.marker = true; });
        apply(inlineSpan.closeStart, inlineSpan.closeEnd, [](Attr& a) { a.marker = true; });
        switch(inlineSpan.kind) {
          case SpanKind::Strong:
            apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) { a.strong = true; });
            break;
          case SpanKind::Emphasis:
            apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) { a.italic = true; });
            break;
          case SpanKind::Strike:
            apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) { a.strike = true; });
            break;
          case SpanKind::Code:
            apply(inlineSpan.contentStart, inlineSpan.contentEnd, [](Attr& a) {
              a.mono = true;
              a.role = TextRole::Code;
            });
            break;
          case SpanKind::Link:
          case SpanKind::Image:
          case SpanKind::Autolink: {
            out.links.push_back(inlineSpan.target);
            const int link = static_cast<int>(out.links.size()) - 1;
            apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link](Attr& a) {
              a.link = link;
              a.role = TextRole::Link;
            });
            break;
          }
          case SpanKind::Escape:
            break;
        }
      }
      appendContentTokens(source, block.contentStart, block.contentEnd, attrs, base, type.mono, revealed, group);
    }
    if(block.end > block.contentEnd) {
      // The trailing newline is always zero width: it must never push the line.
      group.push_back(makeToken(source, block.contentEnd, block.end, markerStyle, TextRole::Marker, true, true, -1));
    }
    groups.push_back(std::move(group));
  }

  Flow flow(metrics_, block.start, out.textLeft, available, lineHeight, !raw && block.kind != BlockKind::Code, padTop, out);
  flow.run(groups);
  float bottom = flow.bottom();
  if(trailingLine) bottom = appendTrailingLine(bottom);
  out.height = bottom + padBottom;
  return out;
}

const BlockLayout* DocumentLayout::layoutForOffset(std::size_t offset, std::size_t* blockIndex) const {
  if(placed_.empty()) return nullptr;
  const std::size_t index = blockIndexAt(blocks_, offset);
  if(blockIndex) *blockIndex = index;
  return placed_[index].layout;
}

Rect DocumentLayout::caretRect(std::size_t offset) const {
  Rect rect {0.0f, 0.0f, 2.0f, options_.type.body * options_.type.lineHeightRatio};
  std::size_t blockIndex = 0;
  const BlockLayout* layout = layoutForOffset(offset, &blockIndex);
  if(!layout || layout->lines.empty()) return rect;
  const float top = placed_[blockIndex].top;
  // Cached runs address their own block, so the caret offset comes down to it.
  const std::size_t local = offset - blocks_[blockIndex].start;

  const TextRun* best = nullptr;
  const VisualLine* bestLine = nullptr;
  const TextRun* before = nullptr;
  const VisualLine* beforeLine = nullptr;
  for(const auto& line : layout->lines) {
    for(const auto& run : line.runs) {
      if(local >= run.srcStart && local < run.srcEnd) {
        best = &run;
        bestLine = &line;
        break;
      }
      if(run.srcEnd <= local) {
        before = &run;
        beforeLine = &line;
      }
    }
    if(best) break;
  }
  if(!best) {
    best = before;
    bestLine = beforeLine;
  }
  if(!best) {
    bestLine = &layout->lines.front();
    rect.x = layout->textLeft;
    rect.y = top + bestLine->y;
    rect.h = bestLine->height;
    return rect;
  }

  float x = best->rect.x;
  if(local > best->srcStart && !best->text.empty()) {
    const std::size_t take = std::min(local - best->srcStart, best->text.size());
    x += metrics_.measure(std::string_view(best->text).substr(0, take), best->style);
  } else if(local >= best->srcEnd) {
    x = best->rect.x + best->rect.w;
  }
  rect.x = x;
  rect.y = top + bestLine->y;
  rect.h = bestLine->height;
  return rect;
}

std::size_t DocumentLayout::offsetAt(float x, float y) const {
  if(flatLines_.empty()) return 0;
  std::size_t flat = 0;
  for(std::size_t i = 0; i < flatLines_.size(); ++i) {
    const auto& entry = flatLines_[i];
    const auto& line = placed_[entry.block].layout->lines[entry.line];
    if(y < entry.top) break;
    flat = i;
    if(y < entry.top + line.height) break;
  }
  const auto& entry = flatLines_[flat];
  const BlockLayout& layout = *placed_[entry.block].layout;
  const VisualLine& line = layout.lines[entry.line];
  const SourceBlock& block = blocks_[entry.block];

  const TextRun* chosen = nullptr;
  for(const auto& run : line.runs) {
    if(run.text.empty()) continue;
    if(!chosen || x >= run.rect.x) chosen = &run;
  }
  if(!chosen) {
    const std::size_t content = block.contentStart - block.start;
    for(const auto& run : line.runs) {
      if(run.srcStart >= content) return block.start + run.srcStart;
    }
    return block.start + (line.runs.empty() ? 0 : line.runs.front().srcStart);
  }
  if(x <= chosen->rect.x) return block.start + chosen->srcStart;

  float pen = chosen->rect.x;
  std::size_t i = 0;
  while(i < chosen->text.size()) {
    const std::size_t next = utf8Next(chosen->text, i);
    const float width = metrics_.measure(std::string_view(chosen->text).substr(i, next - i), chosen->style);
    if(x < pen + width / 2.0f) return block.start + chosen->srcStart + i;
    pen += width;
    i = next;
  }
  return block.start + chosen->srcStart + chosen->text.size();
}

std::vector<Rect> DocumentLayout::selectionRects(std::size_t from, std::size_t to) const {
  std::vector<Rect> rects;
  if(from > to) std::swap(from, to);
  if(from == to) return rects;
  for(const auto& entry : flatLines_) {
    const BlockLayout& layout = *placed_[entry.block].layout;
    const VisualLine& line = layout.lines[entry.line];
    const std::size_t base = blocks_[entry.block].start;
    float left = 0.0f;
    float right = 0.0f;
    bool any = false;
    for(const auto& run : line.runs) {
      if(run.text.empty()) continue;
      const std::size_t runStart = base + run.srcStart;
      const std::size_t runEnd = base + run.srcEnd;
      if(runEnd <= from || runStart >= to) continue;
      const std::size_t a = std::max(from, runStart);
      const std::size_t b = std::min(to, runEnd);
      float x0 = run.rect.x;
      float x1 = run.rect.x + run.rect.w;
      if(a > runStart) {
        x0 += metrics_.measure(std::string_view(run.text).substr(0, std::min(a - runStart, run.text.size())), run.style);
      }
      if(b < runEnd) {
        x1 = run.rect.x + metrics_.measure(std::string_view(run.text).substr(0, std::min(b - runStart, run.text.size())), run.style);
      }
      if(!any) {
        left = x0;
        right = x1;
        any = true;
      } else {
        left = std::min(left, x0);
        right = std::max(right, x1);
      }
    }
    if(any) rects.push_back({left, placed_[entry.block].top + line.y, std::max(2.0f, right - left), line.height});
  }
  return rects;
}

std::optional<std::size_t> DocumentLayout::blockAt(float y) const {
  if(placed_.empty()) return std::nullopt;
  if(y < 0.0f || y > totalHeight_) return std::nullopt;
  for(std::size_t i = 0; i < placed_.size(); ++i) {
    const float top = placed_[i].top;
    if(y >= top && y < top + placed_[i].layout->height) return i;
  }
  return placed_.size() - 1;
}

std::size_t DocumentLayout::flatLineForOffset(std::size_t offset, float* caretX) const {
  const Rect caret = caretRect(offset);
  if(caretX) *caretX = caret.x;
  std::size_t best = 0;
  for(std::size_t i = 0; i < flatLines_.size(); ++i) {
    if(flatLines_[i].top <= caret.y + 0.5f) best = i;
    else break;
  }
  return best;
}

std::size_t DocumentLayout::rowRelative(std::size_t offset, int deltaRows) const {
  if(flatLines_.empty()) return offset;
  float caretX = 0.0f;
  const std::size_t current = flatLineForOffset(offset, &caretX);
  const long long target = static_cast<long long>(current) + deltaRows;
  if(target < 0) return 0;
  if(target >= static_cast<long long>(flatLines_.size())) return source_.size();
  const auto& entry = flatLines_[static_cast<std::size_t>(target)];
  const auto& line = placed_[entry.block].layout->lines[entry.line];
  return offsetAt(caretX, entry.top + line.height / 2.0f);
}

std::size_t DocumentLayout::rowsPerHeight(float height) const {
  const float step = std::max(1.0f, options_.type.body * options_.type.lineHeightRatio);
  return static_cast<std::size_t>(std::max(1.0f, std::floor(height / step)));
}

}
