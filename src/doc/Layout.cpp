#include "doc/Layout.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/Fold.h"

#include "core/util/Hash.h"
#include "core/util/StringUtil.h"
#include "core/util/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace micronotes::doc {
namespace {

// A block's fingerprint is recomputed for every block on every keystroke, so
// the word-at-a-time loop matters here; it lives in core because three other
// caches want the same one.
using microcore::util::hashBytes;
using microcore::util::hashValue;
using microcore::util::kFnvOffset;

// The one whitespace predicate, from `core/util/StringUtil.h`. This file and
// the editor's word walk each carried a copy naming four bytes; the shared one
// names six, and a form feed is whitespace in both of the places they are used.
using microcore::util::isAsciiSpace;

// Newlines and tabs become one space each, so a run's text stays byte-aligned
// with the source it came from and prefix measurement maps offsets to pixels.
std::string displayText(std::string_view source) {
  std::string out(source);
  for(char& c : out) {
    if(c == '\n' || c == '\t' || c == '\r') c = ' ';
  }
  return out;
}

using LineGroup = std::vector<Token>;

// Air above and below a picture, so it does not sit on the paragraph that named
// it or on the one after.
constexpr float kImageGap = 8.0f;

// How far past one generation of the document the block cache is allowed to
// run before it is swept. Named because the number is a judgement -- see the
// sweep for what a bigger one was measured to cost.
constexpr std::size_t kSpareEntries = 256;

Token makeToken(std::string_view source, std::size_t start, std::size_t end, const RunStyle& style,
                TextRole role, bool marker, bool hidden, int link, bool lineBreak = false) {
  Token token;
  token.start = start;
  token.end = end;
  token.style = style;
  token.role = role;
  token.isMarker = marker;
  token.hidden = hidden;
  token.link = link;
  token.lineBreak = lineBreak;
  if(!hidden) token.text = displayText(source.substr(start, end - start));
  token.space = !token.text.empty() && std::all_of(token.text.begin(), token.text.end(), [](char c) { return c == ' '; });
  return token;
}

class Flow {
public:
  Flow(const Metrics& metrics, std::size_t base, float textLeft, float width, float lineHeight,
       bool wrap, float top, BlockLayout& out,
       std::vector<std::pair<std::size_t, float>>& pending, std::vector<float>& cluster)
      : metrics_(metrics), base_(base), textLeft_(textLeft), right_(textLeft + width), lineHeight_(lineHeight), wrap_(wrap), y_(top), pending_(pending), cluster_(cluster), out_(out) {
    penX_ = textLeft_;
    pending_.clear();
    cluster_.clear();
    lineFirstRun_ = static_cast<std::uint32_t>(out_.runs.size());
  }

  // Non-const: a token's text is moved into the run it becomes, rather than
  // copied. Every word in the document was being materialised three times --
  // once out of the source, once into the token, once into the run -- and the
  // third of those is pure waste, because a token is emitted exactly once and
  // read never again.
  // `count` rather than `groups.size()`: the buffer belongs to the layout and
  // keeps last block's groups past the live prefix, so that their token storage
  // can be reused rather than freed.
  void run(std::vector<LineGroup>& groups, std::size_t count) {
    for(std::size_t g = 0; g < count; ++g) {
      LineGroup& group = groups[g];
      group_ = &group;
      for(std::size_t i = 0; i < group.size(); ++i) {
        Token& token = group[i];
        if(token.space) {
          // A space ends the cluster that was accumulating, and is the only
          // place a line may break.
          placeCluster();
          // Measured once, here. It used to be measured twice for every word
          // that followed it -- once to decide whether the line fits and once
          // again inside the flush that emits it -- and roughly half a
          // document's tokens are runs of spaces.
          // By index into the group being walked, which is also where it will be
          // emitted from: a held-back space used to be deep-copied -- string and
          // all -- into this queue, for roughly half the tokens in a document.
          pending_.push_back({i, metrics_.measure(token.text, token.style)});
          pendingWidth_ += pending_.back().second;
          // Held rather than taken now. A trailing newline is the block's own
          // terminator and must not add an empty line under it, so the break is
          // applied when the next word arrives -- if one does.
          if(token.lineBreak && wrap_) pendingBreak_ = true;
          continue;
        }
        if(pendingBreak_) {
          // The spaces that ended the line belong to the line they ended.
          flushPending();
          pushLine();
          pendingBreak_ = false;
        }
        // Everything else joins the cluster being built. A hidden marker and an
        // empty run measure zero and still take their place in it, so the
        // offsets they anchor stay with the word they belong to.
        const float width = token.hidden || token.text.empty()
                              ? 0.0f
                              : metrics_.measure(token.text, token.style);
        if(cluster_.empty()) clusterBegin_ = i;
        cluster_.push_back(width);
        clusterWidth_ += width;
      }
      placeCluster();
      flushPending();
      pushLine();
      pendingBreak_ = false;
    }
  }

  float bottom() const {
    return y_;
  }

private:
  void emit(Token& token, float width) {
    TextRun& run = out_.runs.emplace_back();
    run.srcStart = token.start - base_;
    run.srcEnd = token.end - base_;
    run.rect = {penX_, 0.0f, width, lineHeight_};
    run.style = token.style;
    run.role = token.role;
    run.isMarker = token.isMarker;
    run.linkIndex = token.link;
    if(!token.hidden) run.text = std::move(token.text);
    penX_ += width;
  }

  void flushPending() {
    for(const auto& [index, width] : pending_) emit((*group_)[index], width);
    pending_.clear();
    pendingWidth_ = 0.0f;
  }

  // A cluster is a maximal run of consecutive non-space tokens. The tokenizer
  // splits at every change of inline attribute as well as at every space, so
  // `*emphasis*, code` is the tokens `emphasis` and `, code` with nothing
  // between them; breaking there would leave a comma as the first character of
  // a line. The break decision therefore belongs to the cluster as a whole, and
  // that is why the widths are buffered before it is taken.
  void placeCluster() {
    if(cluster_.empty()) return;
    const float column = right_ - textLeft_;
    const bool breakBefore =
      wrap_ && penX_ > textLeft_ && penX_ + pendingWidth_ + clusterWidth_ > right_;
    // Flush first: the held-back spaces belong to the line the cluster is
    // leaving, and a zero-width run must sit after the spaces that precede it
    // or the offset it anchors lands inside them.
    flushPending();
    if(breakBefore) pushLine();
    if(wrap_ && clusterWidth_ > column) {
      // Wider than any line can be, so it has to break inside itself after
      // all -- between its tokens where it can, and mid-word where even one
      // token does not fit.
      for(std::size_t k = 0; k < cluster_.size(); ++k) {
        Token& token = (*group_)[clusterBegin_ + k];
        const float width = cluster_[k];
        if(penX_ > textLeft_ && penX_ + width > right_) pushLine();
        if(width > column && penX_ <= textLeft_) {
          splitWord(token);
          continue;
        }
        emit(token, width);
      }
    } else {
      for(std::size_t k = 0; k < cluster_.size(); ++k) {
        emit((*group_)[clusterBegin_ + k], cluster_[k]);
      }
    }
    cluster_.clear();
    clusterWidth_ = 0.0f;
  }

  void pushLine() {
    // The runs are already in the block's array, in order. Closing a line is
    // recording where it ends -- no vector to allocate, no runs to move, and
    // nothing to free again when the cache drops the block.
    VisualLine line;
    line.y = y_;
    line.height = lineHeight_;
    line.runBegin = lineFirstRun_;
    line.runEnd = static_cast<std::uint32_t>(out_.runs.size());
    lineFirstRun_ = line.runEnd;
    out_.lines.push_back(line);
    y_ += lineHeight_;
    penX_ = textLeft_;
  }

  // A word wider than the whole column is broken at codepoint boundaries so it
  // never disappears past the right edge.
  void splitWord(Token& token) {
    std::size_t i = 0;
    while(i < token.text.size()) {
      std::size_t j = i;
      float accumulated = 0.0f;
      while(j < token.text.size()) {
        const std::size_t next = util::nextBoundary(token.text, j);
        const float width =
          metrics_.measure(std::string_view(token.text).substr(j, next - j), token.style);
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
  // A line ending has been seen and the spaces it came with are still held. See
  // `run`: the break lands when the next word does.
  bool pendingBreak_ = false;
  float y_ = 0.0f;
  float penX_ = 0.0f;
  // Where the line being built started in `out_.runs`.
  std::uint32_t lineFirstRun_ = 0;
  // Whitespace held back until the next word decides whether the line breaks
  // before or after it, as an index into the group being walked plus the width
  // it was measured at. Borrowed from the layout, so it is allocated once for a
  // document rather than once for each of its blocks.
  std::vector<std::pair<std::size_t, float>>& pending_;
  // The unbreakable cluster being accumulated: the widths of a run of
  // consecutive non-space tokens, which are contiguous in the group and so need
  // only their first index recorded. Borrowed from the layout for the same
  // reason `pending_` is.
  std::vector<float>& cluster_;
  std::size_t clusterBegin_ = 0;
  float clusterWidth_ = 0.0f;
  LineGroup* group_ = nullptr;
  float pendingWidth_ = 0.0f;
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

// Tokens a span of content is about to produce, near enough. Words and the
// spaces between them alternate, and English prose runs about five bytes to the
// word, so a span yields roughly one token per three bytes. It only has to be
// close: the point is that the token vector grows once instead of the five or
// six doublings it took to reach a paragraph's worth from empty, which across a
// document was the single largest source of allocation in laying one out.
std::size_t tokenEstimate(std::size_t bytes) {
  return bytes / 3 + 4;
}

// The same split for a block the inline scanner found nothing in, which in
// ordinary prose is most of them. Worth its own loop because the general one
// pays for markup this block does not have: a heap-allocated attribute slot per
// content byte, zero-filled and then read back, to conclude that every byte is
// plain. Kept directly below its general form so the two stay in step.
void appendPlainTokens(std::string_view source, std::size_t from, std::size_t to,
                       const RunStyle& base, LineGroup& out) {
  out.reserve(out.size() + tokenEstimate(to - from));
  std::size_t i = from;
  while(i < to) {
    const bool space = isAsciiSpace(source[i]);
    std::size_t j = i + 1;
    while(j < to && isAsciiSpace(source[j]) == space) ++j;
    const bool endsLine = space && source.substr(i, j - i).find('\n') != std::string_view::npos;
    if(endsLine && j - i > 1) {
      out.push_back(makeToken(source, i, j - 1, base, TextRole::Body, false, true, -1));
      out.push_back(makeToken(source, j - 1, j, base, TextRole::Body, false, false, -1, true));
      i = j;
      continue;
    }
    out.push_back(makeToken(source, i, j, base, TextRole::Body, false, false, -1, endsLine));
    i = j;
  }
}

// Splits `[from, to)` into tokens that share one set of inline attributes, with
// whitespace kept as its own token so wrapping has break opportunities.
void appendContentTokens(std::string_view source, std::size_t from, std::size_t to, const std::vector<Attr>& attrs,
                         const RunStyle& base, float monoSize, bool revealed, LineGroup& out) {
  out.reserve(out.size() + tokenEstimate(to - from));
  std::size_t i = from;
  while(i < to) {
    const Attr& attr = attrs[i - from];
    const bool space = isAsciiSpace(source[i]);
    std::size_t j = i + 1;
    while(j < to && attrs[j - from] == attr && isAsciiSpace(source[j]) == space) ++j;
    const bool hidden = attr.marker && !revealed;
    const RunStyle style = styleFrom(base, attr, monoSize);
    const TextRole role = attr.marker ? TextRole::Marker : attr.role;
    // A line ending inside a block takes however many bytes it took -- the
    // newline itself, plus the indentation of the line continuing it -- and
    // ends the line on screen. All but the last byte are emitted hidden, which
    // is zero width and still addressable, so the source stays byte-aligned
    // with what is drawn.
    const bool endsLine =
      space && !hidden && source.substr(i, j - i).find('\n') != std::string_view::npos;
    if(endsLine && j - i > 1) {
      out.push_back(makeToken(source, i, j - 1, style, role, attr.marker, true, -1));
      out.push_back(makeToken(source, j - 1, j, style, role, attr.marker, false,
                              attr.marker ? -1 : attr.link, true));
      i = j;
      continue;
    }
    out.push_back(makeToken(source, i, j, style, role, attr.marker, hidden,
                            attr.marker ? -1 : attr.link, endsLine));
    i = j;
  }
}

// Into a buffer the caller owns, for the same reason everything else on this
// path is: a fenced code block or a block dropped to raw asked for one of these
// per block, and a vector returned by value is an allocation per block.
void sourceLinesInto(std::string_view source, std::size_t from, std::size_t to,
                     std::vector<std::pair<std::size_t, std::size_t>>* out) {
  std::vector<std::pair<std::size_t, std::size_t>>& lines = *out;
  lines.clear();
  std::size_t start = from;
  while(start < to) {
    auto newline = source.find('\n', start);
    if(newline == std::string_view::npos || newline >= to) newline = to;
    lines.push_back({start, newline});
    start = newline < to ? newline + 1 : to;
    if(newline == to) break;
  }
  if(lines.empty()) lines.push_back({from, to});
}

}

void DocumentLayout::setMetrics(Metrics metrics) {
  metrics_ = std::move(metrics);
  cache_.clear();
  // Every cached block layout was measured with the old faces, so the standing
  // layout no longer describes anything. Without this the reuse check below
  // would happily keep it: the source and the geometry are unchanged, and the
  // one thing that did change is not visible in either.
  built_ = false;
  // And the placement has to go with the cache, because every entry in it is a
  // pointer *into* the cache that was just emptied. The only caller happens to
  // call update() on the next line, so nothing reads the placement in between
  // -- but "safe as long as nobody asks" is a use-after-free waiting for a
  // second caller, and dropping it costs nothing on a path that has already
  // thrown the whole document's layout away. Queries then answer from an empty
  // document, which they are all written to do.
  placed_.clear();
  lineStart_.clear();
  liveKeys_.clear();
  flags_.clear();
  totalHeight_ = 0.0f;
}

const std::vector<SourceBlock>& DocumentLayout::blocks() const {
  return blocks_;
}

BlockSpan DocumentLayout::blocksAt(std::uint64_t sourceRevision) const {
  if(!built_ || sourceRevision == 0 || sourceRevision != sourceRevision_) return {};
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

std::size_t DocumentLayout::lastRelaidBlocks() const {
  return lastRelaid_;
}

// The fold walk itself: from `from` onward, mark every block that a collapsed
// head hides, and report whether it hid any.
//
// One loop, because the two resolvers differ only in where they start and what
// they already know -- and the loop is where both the counters and the
// nested-fold rule live, so two copies is two places for either to drift.
bool DocumentLayout::hideFoldedFrom(const std::vector<SourceBlock>& blocks,
                                    const LayoutOptions& options, std::size_t from,
                                    std::vector<std::uint8_t>& hidden) const {
  perf::addCounter(perf::CounterId::LayoutFoldBlocksResolved, blocks.size() - from);
  bool any = false;
  for(std::size_t i = from; i < blocks.size(); ++i) {
    // A fold nested inside a collapsed one is already hidden, and costs
    // nothing to resolve again.
    if(hidden[i] || !foldableKind(blocks[i].kind)) continue;
    perf::addCounter(perf::CounterId::LayoutFoldQueries);
    if(!options.folded(blocks[i])) continue;
    const std::size_t end = foldEnd(blocks, i);
    for(std::size_t j = i + 1; j < end; ++j) hidden[j] = 1;
    any = any || end > i + 1;
  }
  return any;
}

bool DocumentLayout::resolveFolds(const std::vector<SourceBlock>& blocks,
                                  const LayoutOptions& options,
                                  std::vector<std::uint8_t>* out) const {
  // Fold ranges come from the block structure, so they can only be resolved
  // once the scan is in: the caller names the heads, the layout names the
  // blocks each head swallows.
  std::vector<std::uint8_t>& hidden = *out;
  hidden.assign(blocks.size(), 0);
  if(!options.folded) return false;
  return hideFoldedFrom(blocks, options, 0, hidden);
}

bool DocumentLayout::resolveFoldsAfter(const std::vector<SourceBlock>& blocks,
                                       const LayoutOptions& options, std::size_t carried,
                                       std::vector<std::uint8_t>* out) const {
  std::vector<std::uint8_t>& hidden = *out;
  if(!options.folded || carried == 0 || hidden_.size() < carried) {
    return resolveFolds(blocks, options, out);
  }
  // Back to the last carried block nothing hides. If the block before the edit
  // is hidden, the walk crosses the run to reach the head that hides it -- that
  // head's reach is exactly what the edit can have changed. If it is not, the
  // block before the edit could still *be* a head reaching into it, so the walk
  // steps back one anyway.
  std::size_t from = carried;
  while(from > 0 && hidden_[from - 1] != 0) --from;
  if(from > 0) --from;

  // One pass over the array rather than three: the carried head is copied and
  // only the tail is cleared, where `assign` would memset the whole thing and
  // then have the head written over it.
  hidden.resize(blocks.size());
  if(from > 0) std::memcpy(hidden.data(), hidden_.data(), from);
  std::memset(hidden.data() + from, 0, blocks.size() - from);
  // Whether anything in the carried head is hidden is not asked -- that would
  // be a scan of it, which is the pass this exists to avoid. Carrying the
  // previous answer forward can only leave `anyHidden_` set when nothing is
  // hidden any more, and the one thing that reads it (`foldsAbsent`) also
  // requires the caller to have withdrawn its predicate -- at which point the
  // resolution is a full one and answers exactly.
  // Called first, not short-circuited: the walk *is* the work, and `||` would
  // skip it whenever the carried answer already said something was hidden.
  const bool hidAny = hideFoldedFrom(blocks, options, from, hidden);
  return anyHidden_ || hidAny;
}

// The first and last index at which two byte spans differ, or `{kNone, kNone}`
// when they are equal. The equality case is what happens on every keystroke, so
// it is one `memcmp` rather than a loop; the scan runs only when a fold really
// did move.
static std::pair<std::size_t, std::size_t> diffSpan(const std::uint8_t* a, const std::uint8_t* b,
                                                    std::size_t count) {
  constexpr std::size_t kNone = DocumentLayout::kNone;
  if(count == 0 || std::memcmp(a, b, count) == 0) return {kNone, kNone};
  std::size_t low = 0;
  while(a[low] == b[low]) ++low;
  std::size_t high = count - 1;
  while(a[high] == b[high]) --high;
  return {low, high};
}

editor::TextEdit DocumentLayout::claimFor(const LayoutOptions& options) const {
  const auto& claim = options.editedSpan;
  if(claim.fromRevision == 0 || claim.toRevision == 0) return {};
  if(sourceRevision_ == 0 || options.sourceRevision == 0) return {};
  if(claim.fromRevision != sourceRevision_ || claim.toRevision != options.sourceRevision) return {};
  return claim;
}

DocumentLayout::EditWindow DocumentLayout::matchEdges(std::string_view oldSource,
                                                     std::string_view newSource,
                                                     const editor::TextEdit& claim) {
  // A block at a time through `memcmp`, which is vectorised, then a byte at a
  // time to land exactly. Byte-at-a-time throughout was 70 microseconds over a
  // 200 KB note -- as much as the walk this whole comparison exists to shorten.
  constexpr std::size_t kChunk = 64;
  const std::size_t limit = std::min(oldSource.size(), newSource.size());
  EditWindow window;
  // Where the caller says the edit is, when the claim is about these two
  // buffers. The checks are the whole safety argument: the span has to fit
  // inside both buffers, and the bytes it leaves outside itself have to be the
  // same count on each side -- an insertion of n and a deletion of m leave
  // old.size() - oldEnd == new.size() - newEnd, and nothing else does. A claim
  // that fails either test is discarded rather than trusted, and the loops
  // below then start where they always did.
  //
  // Both loops only widen the matched prefix and suffix -- which narrows the
  // window -- so starting them at the claim gives the same answer it gives from
  // zero whenever the claim is true, and the counter says how much reading it
  // saved.
  std::size_t seededPrefix = 0;
  std::size_t seededSuffix = 0;
  if(claim.fromRevision != 0 && claim.toRevision != 0 && claim.start <= claim.oldEnd &&
     claim.start <= claim.newEnd && claim.oldEnd <= oldSource.size() &&
     claim.newEnd <= newSource.size() &&
     oldSource.size() - claim.oldEnd == newSource.size() - claim.newEnd) {
    perf::addCounter(perf::CounterId::LayoutEditSpansUsed);
    window.prefix = std::min(claim.start, limit);
    window.suffix = std::min(oldSource.size() - claim.oldEnd, limit - window.prefix);
    seededPrefix = window.prefix;
    seededSuffix = window.suffix;
  } else {
    perf::addCounter(perf::CounterId::LayoutEditSpansCompared);
  }
  while(window.prefix + kChunk <= limit &&
        std::memcmp(oldSource.data() + window.prefix, newSource.data() + window.prefix, kChunk) == 0) {
    window.prefix += kChunk;
  }
  while(window.prefix < limit && oldSource[window.prefix] == newSource[window.prefix]) {
    ++window.prefix;
  }

  const std::size_t tailLimit = limit - window.prefix;
  window.suffix = std::min(window.suffix, tailLimit);
  while(window.suffix + kChunk <= tailLimit &&
        std::memcmp(oldSource.data() + oldSource.size() - window.suffix - kChunk,
                    newSource.data() + newSource.size() - window.suffix - kChunk, kChunk) == 0) {
    window.suffix += kChunk;
  }
  while(window.suffix < tailLimit &&
        oldSource[oldSource.size() - 1 - window.suffix] ==
          newSource[newSource.size() - 1 - window.suffix]) {
    ++window.suffix;
  }
  perf::addCounter(perf::CounterId::LayoutEditBytesMatched, window.prefix + window.suffix);
  // What the two passes actually read, as against what they concluded. Without
  // a claim the two are the same number; with one, the difference is the note
  // the caller saved us reading. `seededSuffix` can exceed the final suffix,
  // because the clamp against `tailLimit` only ever reduces it.
  perf::addCounter(perf::CounterId::LayoutEditBytesCompared,
                   (window.prefix - seededPrefix) +
                     (window.suffix > seededSuffix ? window.suffix - seededSuffix : 0));
  return window;
}

void DocumentLayout::rescan(const EditWindow& window, std::size_t previousBytes,
                            std::size_t* headOut, std::size_t* tailOut) {
  *headOut = 0;
  *tailOut = 0;
  // A list to splice into has to exist, and has to have described the buffer the
  // window was measured against. `blocks_` always partitions the source it was
  // scanned from, so where the last block ends is the whole test.
  if(blocks_.empty() || blocks_.back().end() != previousBytes) {
    scanBlocksInto(source_, &blocks_);
    return;
  }

  const std::ptrdiff_t byteShift =
    static_cast<std::ptrdiff_t>(source_.size()) - static_cast<std::ptrdiff_t>(previousBytes);

  // Where the scan resumes. A block's classification can depend on the line
  // that follows it -- a paragraph ends because the next line starts something,
  // a table is a table because of the row under it -- so the finest thing the
  // edit can be said to have touched is the line holding its first changed
  // byte, not the byte. Resuming two blocks above that line is one block of
  // margin over what the argument needs, and costs two cache probes.
  //
  // The line is found in the already-patched buffer, which is allowed because
  // `prefix` is by definition the first byte that differs: everything before it
  // reads the same in either generation.
  const std::size_t changed = std::min(window.prefix, previousBytes);
  std::size_t lineAt = 0;
  if(changed > 0) {
    const std::size_t newline = source_.rfind('\n', changed - 1);
    lineAt = newline == std::string::npos ? 0 : newline + 1;
  }
  const std::size_t holder = blockIndexAt(blocks_, lineAt);
  const std::size_t carried = holder >= 2 ? holder - 2 : 0;
  const std::size_t restart = blocks_[carried].start;

  // Where it may stop. Both halves of this matter: `scanBlocksFrom` asks only
  // once the rest of the buffer is bytes the edit left alone, and this says the
  // previous scan was between blocks at the same place. A scan that carries no
  // state, resumed from a shared boundary over identical bytes, produces
  // identical blocks -- so from there on the answer is the list already in hand.
  //
  // The `> restart` is not decoration. A large insertion can reach the
  // untouched tail after scanning fewer bytes than it added, and the matching
  // old offset would then land *above* where the scan resumed -- which would
  // splice the same blocks in twice.
  const auto resumeAt = [&](std::size_t offset) {
    const std::ptrdiff_t was = static_cast<std::ptrdiff_t>(offset) - byteShift;
    if(was <= static_cast<std::ptrdiff_t>(restart)) return false;
    const std::size_t index = blockIndexAt(blocks_, static_cast<std::size_t>(was));
    return blocks_[index].start == static_cast<std::size_t>(was);
  };

  scanned_.clear();
  const std::size_t stopped =
    scanBlocksFrom(source_, restart, window.suffix, resumeAt, &scanned_);
  const std::size_t oldCount = blocks_.size();
  const std::size_t resume =
    stopped < source_.size()
      ? blockIndexAt(blocks_, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(stopped) -
                                                       byteShift))
      : oldCount;

  // Splice: the head stays where it is, the scanned middle replaces the blocks
  // it re-derived, and the tail slides to meet it.
  const std::size_t middle = scanned_.size();
  const std::size_t count = carried + middle + (oldCount - resume);
  if(count > oldCount) {
    blocks_.resize(count);
    std::move_backward(blocks_.begin() + resume, blocks_.begin() + oldCount, blocks_.end());
  } else if(count < oldCount) {
    std::move(blocks_.begin() + resume, blocks_.begin() + oldCount,
              blocks_.begin() + carried + middle);
    blocks_.resize(count);
  }
  std::move(scanned_.begin(), scanned_.end(), blocks_.begin() + carried);
  // The tail's blocks keep everything except where in the buffer they sit, and
  // that moved by exactly the number of bytes the edit added or removed. One
  // integer add per block -- a block's length and its payload are held from its
  // own start, so moving the block moves all four offsets at once -- against a
  // hash of every byte and a map probe each if they were re-derived instead.
  for(std::size_t i = carried + middle; i < count; ++i) {
    SourceBlock& block = blocks_[i];
    block.start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(block.start) + byteShift);
  }

  perf::addCounter(perf::CounterId::LayoutBlocksRescanned, middle);
  if(blocks_.empty()) {
    // An emptied buffer is still one block, so there is somewhere to put a
    // caret. Same rule the full scan ends on, and nothing carried over.
    blocks_.emplace_back();
    return;
  }
  *headOut = carried;
  *tailOut = oldCount - resume;
}

std::size_t DocumentLayout::blockIndexFor(std::size_t offset) const {
  if(offset == kNone) return kNone;
  return blockIndexAt(blocks_, std::min(offset, source_.size()));
}

// A memcmp of the whole note, which is O(document) -- but it is one linear pass
// over bytes already in cache, and what it decides is whether to skip the copy,
// the rescan, the per-block key hash and the flat-line rebuild. On a 235 KB note
// that trade is about 12 microseconds against 1.9 milliseconds.
bool DocumentLayout::sourceMatches(std::string_view source) const {
  if(!built_) return false;
  if(source.size() != source_.size()) return false;
  return source.empty() || std::memcmp(source.data(), source_.data(), source.size()) == 0;
}

// Whether the layout already standing is the exact answer to this call.
//
// "Exact" has to cover every input the block cache keys on, because the whole
// point is to skip building those keys: the geometry hash, which block holds the
// caret (markers are revealed per block, not per offset), which block is shown
// raw, revealAll, and the resolved fold state. The source bytes are the caller's
// precondition -- this is asked only once `sourceMatches` has said yes.
//
// Two things are deliberately NOT covered, both because the block cache does not
// cover them either, so nothing regresses by skipping them here:
//   - `wikiLinkResolves`. Its answer decides a run's role but is not part of a
//     block's cache key, so a link that starts or stops resolving does not
//     invalidate the cached block today either. It shows up on the next edit.
//   - `metrics_`. Installing new metrics drops the cache and clears `built_`,
//     which is the invalidation.
bool DocumentLayout::canReuse(const LayoutOptions& options, std::uint64_t geometry,
                              bool foldsMatch) const {
  if(!foldsMatch) return false;
  if(geometry != geometryHash_) return false;
  if(options.revealAll != options_.revealAll) return false;
  if(blockIndexFor(options.caretOffset) != caretBlock_) return false;
  return blockIndexFor(options.rawOffset) == rawBlock_;
}

DocumentLayout::Flags DocumentLayout::flagsFor(std::size_t index, std::size_t caretBlock,
                                               std::size_t rawBlock) const {
  const SourceBlock& block = blocks_[index];
  Flags flags;
  flags.revealed = options_.revealAll || index == caretBlock;
  flags.raw = index == rawBlock;
  flags.first = index == 0;
  // A buffer ending in a newline has one more (empty) line to put a caret on.
  flags.trailingLine = index + 1 == blocks_.size() && block.end() == source_.size() &&
                       !source_.empty() && source_.back() == '\n';
  flags.hidden = hidden_[index] != 0;
  flags.groupFirst = startsQuoteRun(blocks_, index);
  flags.groupLast = endsQuoteRun(blocks_, index);
  return flags;
}

const BlockLayout* DocumentLayout::resolveEntry(std::size_t index, const Flags& flags,
                                                std::uint64_t geometry, std::uint64_t* key,
                                                Tally* tally) {
  const SourceBlock& block = blocks_[index];
  tally->keyBytes += block.end() - block.start;
  std::uint64_t hash =
    hashBytes(geometry, source_.data() + block.start, block.end() - block.start);
  hash = hashValue(hash, block.kind);
  hash = hashValue(hash, block.level);
  hash = hashValue(hash, block.listDepth);
  hash = hashValue(hash, block.ordinal);
  hash = hashValue(hash, block.checked);
  // Hashed as bytes, which is only correct while the struct has no padding in
  // it: padding bytes are indeterminate, so hashing them mixes whatever the
  // stack held into a cache key -- and the failure mode is a block that hashes
  // to two keys under the same flags, which is a cache that quietly stops
  // hitting rather than anything that looks like a bug. Seven bools have no
  // padding; adding a wider field to `Flags` would introduce some, and this is
  // what makes that a build error rather than a slow afternoon.
  static_assert(sizeof(Flags) == 7 * sizeof(bool),
                "Flags is hashed as raw bytes and must have no padding");
  hash = hashBytes(hash, &flags, sizeof(flags));
  *key = hash;

  auto found = cache_.find(hash);
  if(found == cache_.end()) {
    ++lastRelaid_;
    found = cache_.emplace(hash, layoutBlock(index, flags)).first;
  } else {
    ++tally->cacheHits;
  }
  return &found->second;
}

// Everything about the *shape* a block would be laid out in, as one number.
//
// It seeds every cache key, so a layout built under a different one shares
// nothing with this call -- which is why `patchable` requires it to match. Every
// field of `LayoutOptions` that is not the source, the caret or the folds
// belongs here, and forgetting one is a stale layout kept under a key that no
// longer describes it.
std::uint64_t DocumentLayout::geometryKey(const LayoutOptions& options) {
  std::uint64_t geometry = kFnvOffset;
  geometry = hashValue(geometry, options.width);
  geometry = hashValue(geometry, options.fontScale);
  geometry = hashValue(geometry, options.indentStep);
  geometry = hashValue(geometry, options.listGutter);
  geometry = hashValue(geometry, options.quoteGutter);
  geometry = hashValue(geometry, options.blockSpacing);
  geometry = hashValue(geometry, options.headingSpaceAbove);
  // Same rule as `Flags` below: raw-byte hashed, so it must stay padding-free.
  static_assert(sizeof(TypeMetrics) == 9 * sizeof(float),
                "TypeMetrics is hashed as raw bytes and must have no padding");
  geometry = hashBytes(geometry, &options.type, sizeof(options.type));
  geometry = hashValue(geometry, options.wikiLinkRevision);
  geometry = hashValue(geometry, options.imageMaxHeight);
  geometry = hashValue(geometry, options.imageRevision);
  return geometry;
}

// Sorted and merged, so the walk sees each block at most once and the gaps
// between ranges are real gaps. Adjacent ranges merge as well as overlapping
// ones: a gap of nothing is not a gap worth reconverging across.
void DocumentLayout::mergeDirtyRanges() {
  std::sort(dirty_.begin(), dirty_.end());
  std::size_t ranges = 0;
  for(std::size_t i = 0; i < dirty_.size(); ++i) {
    if(ranges > 0 && dirty_[i].first <= dirty_[ranges - 1].second + 1) {
      dirty_[ranges - 1].second = std::max(dirty_[ranges - 1].second, dirty_[i].second);
    } else {
      dirty_[ranges++] = dirty_[i];
    }
  }
  dirty_.resize(ranges);
}

void DocumentLayout::sweepLayoutCache() {
  // Bounded memory: keep the live generation of block layouts and a small spare.
  // The sweep is where a resize spends its worst frame, because it frees layouts
  // in bulk -- every line, run and run string of the ones it drops -- so it gets
  // its own timer rather than hiding inside the update's.
  //
  // The ceiling was `blocks * 3 + 256` on the theory that three generations buy
  // back the case where a key returns: an undo, a retype, a window dragged back
  // to a width it just left. Measured on the 200 KB fixture, interleaved, it
  // buys nothing and costs a lot. At one generation `layout.blocks_relaid` is
  // *identical* -- not one extra block was laid out, because on that workload no
  // key ever came back -- while peak RSS goes from 55.1 MB to 28.7 MB.
  //
  // The other half is the shape of the work rather than the amount. Fourteen
  // width steps at three generations are one sweep freeing 26,864 layouts, which
  // is a single 24 ms frame and is the whole of the worst frame of a window drag.
  // At one generation they are nine sweeps of about 5,400 each: 24 ms of `free`
  // in total instead of 11, and no frame over 11 ms. Total work up, spike down,
  // and the spike is the part anyone sees.
  //
  // The spare `kSpareEntries` is what keeps an undo of a keystroke hitting: an
  // edit adds about one key, so a sweep runs at most every `kSpareEntries + 1`
  // of them.
  if(cache_.size() > blocks_.size() + kSpareEntries) {
    const perf::ScopeTimer evictTimer("layout.update.evict_cache");
    perf::addCounter(perf::CounterId::LayoutCacheSweeps);
    liveSorted_ = liveKeys_;
    std::sort(liveSorted_.begin(), liveSorted_.end());
    std::uint64_t evicted = 0;
    for(auto it = cache_.begin(); it != cache_.end();) {
      if(std::binary_search(liveSorted_.begin(), liveSorted_.end(), it->first)) {
        ++it;
      } else {
        ++evicted;
        it = cache_.erase(it);
      }
    }
    perf::addCounter(perf::CounterId::LayoutCacheEvictions, evicted);
    // No re-pointing of `placed_` afterwards. `unordered_map` is node-based:
    // erasing an element invalidates pointers into that element only, and every
    // key in `liveKeys_` survived the sweep by construction. The loop that used
    // to sit here re-found all ten thousand of them -- one hash probe per block,
    // which is the cost the whole key-reuse path exists to avoid.
  }
}

// One call of `update`, with the five phases as its methods.
//
// `update` was 353 lines and one algorithm: decide whether the standing
// partition still describes the source, absorb the edit, align the placement to
// the new indexing, walk the dirty ranges, carry the outstanding shift down.
// Each phase read and wrote locals the next one depended on -- `head`, `tail`,
// `patchable`, `geometry`, `caretBlock`, `rawBlock`, `count`, `previousCount`,
// `shift`, `pendingTop`, `pendingRows`, `settled` -- which is exactly the shape
// that says a function is one algorithm rather than several: extracting a phase
// meant passing eight or nine of them.
//
// So the state is the carrier and the phases are its methods. What made getting
// this right delicate is the ownership, and the rule is one line: **the pass
// owns nothing.** The standing arrays are the layout's -- `placed_`, `flags_`,
// `liveKeys_`, `lineStart_`, `hidden_`, `dirty_` are patched in place through
// `doc_`, never copied in and back out -- and what lives here is only the
// bookkeeping that was on the stack before. A carrier holding a generation of
// the document would be the function it replaced with a copy of the note added.
//
// It lives for one call and holds `doc_` and `opts_` by reference for that
// reason. `incoming_` is the caller's buffer, which is deliberately not
// `doc_.source_`: telling those two apart is what the whole first phase is
// about.
class DocumentLayout::UpdatePass {
public:
  UpdatePass(DocumentLayout& doc, std::string_view source, const LayoutOptions& options)
      : doc_(doc), incoming_(source), opts_(options) {}

  void run() {
    geometry_ = geometryKey(opts_);
    previousCount_ = doc_.blocks_.size();
    // A caller that stamps its buffer is believed; one that does not gets the
    // memcmp. The stamp is checked first so the common case -- an idle frame
    // over an unedited note -- does not touch the document at all.
    sourceStamped_ = opts_.sourceRevision != 0 && doc_.built_ &&
                     opts_.sourceRevision == doc_.sourceRevision_;
    // Whether the standing placement can be *patched* rather than rebuilt. The
    // geometry seeds every cache key, so a layout built under a different one
    // shares nothing with this call; and the four parallel arrays have to
    // describe the blocks standing now, or there is nothing to patch.
    patchable_ = doc_.built_ && geometry_ == doc_.geometryHash_ &&
                 doc_.flags_.size() == previousCount_ &&
                 doc_.liveKeys_.size() == previousCount_ &&
                 doc_.placed_.size() == previousCount_ &&
                 doc_.hidden_.size() == previousCount_ &&
                 doc_.lineStart_.size() == previousCount_ + 1;
    // Read before `options_` is overwritten below: a `revealAll` flip changes
    // every block's flags at once, which is the one change with no local extent.
    revealAllChanged_ = doc_.built_ && opts_.revealAll != doc_.options_.revealAll;

    // The blocks whose entry this call can have moved. Collected as ranges,
    // sorted and merged below; everything outside them is provably identical to
    // what is already standing, which is the whole basis of the patch.
    doc_.dirty_.clear();

    if(!absorbSource()) return;

    doc_.options_ = opts_;
    count_ = doc_.blocks_.size();
    // The carried-over ends say which blocks kept their entry, and that is only
    // a claim about a placement there is one of. Without one, nothing carries.
    if(!patchable_) {
      head_ = 0;
      tail_ = 0;
    }
    shift_ = static_cast<std::ptrdiff_t>(count_) - static_cast<std::ptrdiff_t>(previousCount_);

    markMovedBlocks();

    const perf::ScopeTimer placeTimer("layout.update.place_blocks");
    alignPlacement();
    doc_.mergeDirtyRanges();
    walkDirtyRanges();
    publish();
  }

private:
  void markDirty(std::size_t low, std::size_t high) {
    if(low == kNone) return;
    doc_.dirty_.emplace_back(low, high == kNone ? low : high);
  }

  // --- phase one: does the standing partition still describe the source? ----
  //
  // False when the call is already answered: nothing the layout depends on
  // moved, so every byte this would have copied, scanned, hashed and walked
  // would have reproduced what is already sitting in `placed_`.
  bool absorbSource() {
    // Identical bytes mean an identical partition, so `blocks_` still describes
    // this source and the fold state can be resolved against it directly. That
    // is the common case by a wide margin: the live surface re-lays the note out
    // once per frame whether or not anything happened, and a scroll is every
    // frame with nothing happening.
    if(sourceStamped_ || doc_.sourceMatches(incoming_)) return absorbUnchangedSource();
    absorbEdit();
    return true;
  }

  bool absorbUnchangedSource() {
    // Same bytes means the same partition, so the standing fold resolution
    // describes this call too -- if nothing about the folds has moved. A stamped
    // caller says so directly; an unstamped one has to be asked block by block.
    const bool foldsStamped =
      opts_.foldRevision != 0 && opts_.foldRevision == doc_.foldRevision_;
    // A caller offering no predicate is saying nothing is folded. If nothing was
    // folded last time either, the resolution is the same all-zero array it
    // already is -- so there is nothing to build and nothing to compare it to.
    const bool foldsAbsent =
      !opts_.folded && !doc_.anyHidden_ && doc_.hidden_.size() == doc_.blocks_.size();
    std::pair<std::size_t, std::size_t> foldDiff {kNone, kNone};
    if(foldsAbsent) {
      perf::addCounter(perf::CounterId::LayoutFoldResolutionsSkipped);
    } else if(!foldsStamped) {
      const perf::ScopeTimer foldTimer("layout.update.resolve_folds");
      doc_.anyHidden_ = doc_.resolveFolds(doc_.blocks_, opts_, &doc_.spareHidden_);
      foldDiff = diffSpan(doc_.spareHidden_.data(), doc_.hidden_.data(),
                          std::min(doc_.spareHidden_.size(), doc_.hidden_.size()));
      if(doc_.spareHidden_.size() != doc_.hidden_.size()) {
        foldDiff = {0, doc_.spareHidden_.size()};
      }
    }
    if(doc_.canReuse(opts_, geometry_, foldDiff.first == kNone)) {
      // Before this returned early it was the single largest cost in a frame,
      // and on a scroll -- where by definition only the viewport moved -- it was
      // the whole frame's work.
      perf::addCounter(perf::CounterId::LayoutUnchangedUpdates);
      // The predicates are fresh closures every frame even when their answers
      // are not, so the stored options have to take them, or a later query would
      // call through a capture that has gone.
      doc_.options_ = opts_;
      doc_.lastRelaid_ = 0;
      return false;
    }
    // Something else moved -- the caret, the width, a fold -- so the blocks have
    // to be placed again. The scan and the copy do not: those are the document,
    // and the document is what did not change. Every block maps to itself.
    // When the folds are stamped unchanged, `hidden_` already describes them.
    if(foldDiff.first != kNone) {
      // Which blocks a fold change moved, rather than all of them: a fold hides
      // a run, and the run is exactly where the flags differ.
      markDirty(foldDiff.first, foldDiff.second);
      doc_.hidden_.swap(doc_.spareHidden_);
    }
    head_ = doc_.blocks_.size();
    return true;
  }

  void absorbEdit() {
    // What the edit did, measured against the buffer the layout is standing on
    // rather than against a copy of it. Taking this window first is what lets
    // everything below be a patch: the source, the block list and the placement
    // are all carried forward through it instead of rebuilt.
    const EditWindow window = matchEdges(doc_.source_, incoming_, doc_.claimFor(opts_));
    const std::size_t previousBytes = doc_.source_.size();
    spliceSource(window, previousBytes);
    {
      const perf::ScopeTimer scanTimer("layout.update.scan_blocks");
      doc_.rescan(window, previousBytes, &head_, &tail_);
    }
    perf::addCounter(perf::CounterId::LayoutBlocksScanned, doc_.blocks_.size());
    // Same question on the unchanged path, where the block count moved: an
    // all-zero resolution of the new length is still the same answer, so only
    // the array has to be resized, and neither diff below can find anything.
    const bool foldsAbsent = !opts_.folded && !doc_.anyHidden_;
    if(foldsAbsent) {
      perf::addCounter(perf::CounterId::LayoutFoldResolutionsSkipped);
      doc_.spareHidden_.assign(doc_.blocks_.size(), 0);
    } else {
      const perf::ScopeTimer foldTimer("layout.update.resolve_folds");
      // Resumed at the seam rather than restarted at the top: an edit at the
      // bottom of a folded note re-resolves the blocks after it, not the note.
      doc_.anyHidden_ = doc_.resolveFoldsAfter(doc_.blocks_, opts_, patchable_ ? head_ : 0,
                                               &doc_.spareHidden_);
    }
    const std::size_t count = doc_.blocks_.size();
    // The blocks between the two carried-over ends are the edit itself, and the
    // block either side of them can have changed which quote run it belongs to
    // -- that is the one flag decided by a neighbour rather than by the block.
    markDirty(head_ > 0 ? head_ - 1 : 0, std::min(count - tail_, count - 1));
    if(patchable_ && !foldsAbsent) {
      // The fold state has to be diffed across the edit's index shift: the
      // blocks before it kept their index, the ones after it moved by the change
      // in block count, and the ones between are being rebuilt anyway.
      const auto headDiff = diffSpan(doc_.spareHidden_.data(), doc_.hidden_.data(), head_);
      markDirty(headDiff.first, headDiff.second);
      const auto tailDiff = diffSpan(doc_.spareHidden_.data() + count - tail_,
                                     doc_.hidden_.data() + previousCount_ - tail_, tail_);
      if(tailDiff.first != kNone) {
        markDirty(count - tail_ + tailDiff.first, count - tail_ + tailDiff.second);
      }
    }
    doc_.hidden_.swap(doc_.spareHidden_);
  }

  // The layout keeps its own copy of the buffer because every run of every
  // cached block points into it, and the caller's buffer is not the layout's to
  // hold. Keeping that copy current used to `assign` the whole note on every
  // keystroke -- 200 KB per typed character. The window says which bytes
  // actually moved, so this writes those and memmoves what follows them.
  void spliceSource(const EditWindow& window, std::size_t previousBytes) {
    const std::size_t from = window.prefix;
    const std::size_t removed = previousBytes - window.suffix - from;
    const std::size_t added = incoming_.size() - window.suffix - from;
    perf::addCounter(perf::CounterId::LayoutSourceBytesCopied, added);
    if(added != removed) {
      perf::addCounter(perf::CounterId::LayoutSourceBytesMoved, window.suffix);
    }
    // `replace` has undefined behaviour if `incoming_` views our own buffer. No
    // caller does that -- and one that handed us back exactly our own bytes
    // would have been answered by `sourceMatches` above -- but a view *into* it
    // would corrupt the copy silently, so it costs two comparisons to say so
    // instead.
    const auto address = [](const char* pointer) {
      return reinterpret_cast<std::uintptr_t>(pointer);
    };
    if(address(incoming_.data()) >= address(doc_.source_.data()) &&
       address(incoming_.data()) <= address(doc_.source_.data()) + doc_.source_.size()) {
      doc_.source_ = std::string(incoming_);
    } else {
      doc_.source_.replace(from, removed, incoming_.data() + from, added);
    }
  }

  // --- phase two: which blocks did the caret and the raw block move? --------

  // Where a block that stood at old index `index` sits now, or `kNone` when the
  // edit rebuilt it -- in which case it is inside the dirty middle already.
  std::size_t nowAt(std::size_t index) const {
    if(index == kNone || index >= previousCount_) return kNone;
    if(index < head_) return index;
    if(index + tail_ >= previousCount_) {
      return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + shift_);
    }
    return kNone;
  }

  void markMovedBlocks() {
    caretBlock_ = doc_.blockIndexFor(opts_.caretOffset);
    rawBlock_ = doc_.blockIndexFor(opts_.rawOffset);
    if(revealAllChanged_) {
      // Every block reveals or hides its markers at once. Nothing local about it.
      markDirty(0, count_ - 1);
    } else if(nowAt(doc_.caretBlock_) != caretBlock_) {
      // Two blocks: the one the caret left stops showing its markers, and the
      // one it arrived at starts.
      markDirty(caretBlock_ == kNone ? kNone : caretBlock_, kNone);
      markDirty(nowAt(doc_.caretBlock_), kNone);
    }
    if(nowAt(doc_.rawBlock_) != rawBlock_) {
      markDirty(rawBlock_ == kNone ? kNone : rawBlock_, kNone);
      markDirty(nowAt(doc_.rawBlock_), kNone);
    }
  }

  // --- phase three: align the standing arrays to the new indexing -----------

  void alignPlacement() {
    if(!patchable_) {
      // Nothing to patch: no standing layout, or one built under another
      // geometry. The walk below rebuilds every entry, which is what no
      // carried-over ends and a dirty range covering the document ask it to do.
      resizeArrays();
      doc_.dirty_.clear();
      doc_.dirty_.emplace_back(0, count_ - 1);
      return;
    }
    if(shift_ == 0) return;
    // The blocks after the edit kept their content, their flags, their key and
    // the layout behind it, and moved by the change in block count -- so moving
    // them is a memmove of four parallel arrays, where rebuilding them is a hash
    // of every byte and a map probe per block.
    const std::size_t oldTail = previousCount_ - tail_;
    if(shift_ > 0) {
      resizeArrays();
      std::move_backward(doc_.placed_.begin() + oldTail, doc_.placed_.begin() + previousCount_,
                         doc_.placed_.end());
      std::move_backward(doc_.flags_.begin() + oldTail, doc_.flags_.begin() + previousCount_,
                         doc_.flags_.end());
      std::move_backward(doc_.liveKeys_.begin() + oldTail, doc_.liveKeys_.begin() + previousCount_,
                         doc_.liveKeys_.end());
      std::move_backward(doc_.lineStart_.begin() + oldTail,
                         doc_.lineStart_.begin() + previousCount_ + 1, doc_.lineStart_.end());
      return;
    }
    const std::size_t newTail = count_ - tail_;
    std::move(doc_.placed_.begin() + oldTail, doc_.placed_.begin() + previousCount_,
              doc_.placed_.begin() + newTail);
    std::move(doc_.flags_.begin() + oldTail, doc_.flags_.begin() + previousCount_,
              doc_.flags_.begin() + newTail);
    std::move(doc_.liveKeys_.begin() + oldTail, doc_.liveKeys_.begin() + previousCount_,
              doc_.liveKeys_.begin() + newTail);
    std::move(doc_.lineStart_.begin() + oldTail, doc_.lineStart_.begin() + previousCount_ + 1,
              doc_.lineStart_.begin() + newTail);
    resizeArrays();
  }

  // The four parallel arrays and the row index, to the block count this call
  // settled on. `lineStart_` carries one extra: the document's total row count.
  void resizeArrays() {
    doc_.placed_.resize(count_);
    doc_.flags_.resize(count_);
    doc_.liveKeys_.resize(count_);
    doc_.lineStart_.resize(count_ + 1);
  }

  // --- phase four: walk what moved, and carry the shift down ---------------

  void walkDirtyRanges() {
    doc_.lastRelaid_ = 0;
    for(const auto& range : doc_.dirty_) {
      const std::size_t low = range.first;
      const std::size_t high = std::min(range.second, count_ - 1);
      if(low > high) continue;
      carryShiftTo(low);
      relayRange(low, high);
      settled_ = high + 1;
    }
    carryShiftToEnd();
  }

  // Carry the outstanding shift down to the start of the next dirty range.
  // Positions only; these blocks' entries are untouched.
  void carryShiftTo(std::size_t low) {
    if(pendingTop_ == 0.0f && pendingRows_ == 0) return;
    for(std::size_t i = settled_; i < low; ++i) {
      doc_.placed_[i].top += pendingTop_;
      doc_.lineStart_[i] = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(doc_.lineStart_[i]) + pendingRows_);
    }
    shifted_ += low - settled_;
  }

  void relayRange(std::size_t low, std::size_t high) {
    // The first block of the document starts at zero by definition; any other
    // takes its position from the block above, which is settled by now.
    float top = low == 0 ? 0.0f : doc_.placed_[low].top + pendingTop_;
    std::int64_t rows =
      low == 0 ? 0 : static_cast<std::int64_t>(doc_.lineStart_[low]) + pendingRows_;
    for(std::size_t i = low; i <= high; ++i) {
      const Flags flags = doc_.flagsFor(i, caretBlock_, rawBlock_);
      const BlockLayout* layout = nullptr;
      // A block from one of the carried-over ends is at the index its entry is
      // already filed under -- the alignment above moved the tail's entries to
      // meet it -- so its standing key and layout describe it still.
      const bool carriedOver = i < head_ || i >= count_ - tail_;
      if(carriedOver && doc_.flags_[i] == flags) {
        // An identical block under identical flags has an identical key, and the
        // layout it resolved to last time is still in the cache under it. This
        // is what makes a conservative dirty range cheap: no bytes hashed, and
        // -- the part that actually costs -- no random probe into a map with one
        // entry per block in the note.
        ++keyReused_;
        layout = doc_.placed_[i].layout;
      } else {
        std::uint64_t key = 0;
        layout = doc_.resolveEntry(i, flags, geometry_, &key, &tally_);
        doc_.liveKeys_[i] = key;
        doc_.flags_[i] = flags;
      }
      doc_.placed_[i].top = top;
      doc_.placed_[i].layout = layout;
      doc_.lineStart_[i] = static_cast<std::uint32_t>(rows);
      top += layout->height;
      rows += static_cast<std::int64_t>(layout->lines.size());
    }
    walked_ += high - low + 1;
    if(high + 1 < count_) {
      // What this range moved everything below it by. Zero is the common case
      // and the whole point: typing a character inside a paragraph that does not
      // rewrap leaves the rest of the document already correct.
      pendingTop_ = top - doc_.placed_[high + 1].top;
      pendingRows_ = rows - static_cast<std::int64_t>(doc_.lineStart_[high + 1]);
    } else {
      doc_.totalHeight_ = top;
      doc_.lineStart_[count_] = static_cast<std::uint32_t>(rows);
      pendingTop_ = 0.0f;
      pendingRows_ = 0;
    }
  }

  void carryShiftToEnd() {
    if(pendingTop_ == 0.0f && pendingRows_ == 0) return;
    for(std::size_t i = settled_; i < count_; ++i) {
      doc_.placed_[i].top += pendingTop_;
      doc_.lineStart_[i] = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(doc_.lineStart_[i]) + pendingRows_);
    }
    shifted_ += count_ - settled_;
    doc_.lineStart_[count_] = static_cast<std::uint32_t>(
      static_cast<std::int64_t>(doc_.lineStart_[count_]) + pendingRows_);
    doc_.totalHeight_ += pendingTop_;
  }

  // --- phase five: post what happened, and stamp what it happened to --------

  void publish() {
    perf::addCounter(perf::CounterId::LayoutBlocksWalked, walked_);
    perf::addCounter(perf::CounterId::LayoutBlocksShifted, shifted_);
    perf::addCounter(perf::CounterId::LayoutBlocksKeyReused, keyReused_);
    perf::addCounter(perf::CounterId::LayoutKeyBytesHashed, tally_.keyBytes);
    perf::addCounter(perf::CounterId::LayoutBlocksRelaid, doc_.lastRelaid_);
    perf::addCounter(perf::CounterId::LayoutCacheHits, tally_.cacheHits);
    perf::addCounter(perf::CounterId::LayoutVisualRows, doc_.lineStart_[count_]);
    perf::addCounter(patchable_ ? perf::CounterId::LayoutPlacementPatches
                                : perf::CounterId::LayoutPlacementRebuilds);

    doc_.geometryHash_ = geometry_;
    doc_.sourceRevision_ = opts_.sourceRevision;
    doc_.foldRevision_ = opts_.foldRevision;
    doc_.caretBlock_ = caretBlock_;
    doc_.rawBlock_ = rawBlock_;
    doc_.built_ = true;

    doc_.sweepLayoutCache();
  }

  DocumentLayout& doc_;
  // The caller's buffer. Deliberately not `doc_.source_`: telling the two apart
  // is what the first phase is about.
  std::string_view incoming_;
  const LayoutOptions& opts_;

  std::uint64_t geometry_ = 0;
  std::size_t previousCount_ = 0;
  bool sourceStamped_ = false;
  bool patchable_ = false;
  bool revealAllChanged_ = false;

  // How many blocks came through this call unchanged at each end of the
  // document. Everything between them is the extent of what moved, and the two
  // numbers together are what used to be a `size_t` per block.
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t count_ = 0;
  std::ptrdiff_t shift_ = 0;
  std::size_t caretBlock_ = kNone;
  std::size_t rawBlock_ = kNone;

  // What the walk found, posted once by `publish`.
  Tally tally_;
  std::uint64_t keyReused_ = 0;
  std::uint64_t walked_ = 0;
  std::uint64_t shifted_ = 0;
  // What the blocks recomputed so far have added to every position below them,
  // held back rather than applied: a keystroke that does not change its block's
  // height or line count leaves both zero, and then there is nothing below the
  // edit to touch at all.
  float pendingTop_ = 0.0f;
  std::int64_t pendingRows_ = 0;
  std::size_t settled_ = 0;  // every entry below this index is correct
};

void DocumentLayout::update(std::string_view source, const LayoutOptions& options) {
  const perf::ScopeTimer timer("layout.update");
  perf::addCounter(perf::CounterId::LayoutUpdateCalls);
  UpdatePass(*this, source, options).run();
}

DocumentLayout::BlockStyle DocumentLayout::styleForBlock(const SourceBlock& block,
                                                        const Flags& flags,
                                                        BlockLayout& out) const {
  const TypeMetrics& type = options_.type;
  BlockStyle style;
  style.base.size = type.body;
  style.padBottom = options_.blockSpacing;
  out.indent = static_cast<float>(block.listDepth) * options_.indentStep;
  out.textLeft = out.indent;

  switch(block.kind) {
    case BlockKind::Heading:
      style.base.size = type.heading[std::clamp<int>(block.level, 1, 6) - 1];
      style.base.strong = true;
      if(!flags.first) style.padTop = options_.headingSpaceAbove;
      break;
    case BlockKind::Bullet:
    case BlockKind::Ordered:
      out.textLeft = out.indent + options_.listGutter;
      break;
    case BlockKind::Todo:
      out.textLeft = out.indent + options_.listGutter;
      // A ticked task is struck through. Only while its marker is hidden: with
      // the caret in the block the `- [x] ` is text the user is editing, and a
      // line through what you are typing is a line through your own cursor.
      if(block.checked && !flags.revealed && !flags.raw) style.base.strike = true;
      break;
    case BlockKind::Quote:
    case BlockKind::Callout:
      out.textLeft = out.indent + options_.quoteGutter;
      // Padding belongs to the run, not to every line in it, or a three-line
      // callout would be drawn with three lots of air inside its own box.
      style.padTop = flags.groupFirst ? 8.0f : 0.0f;
      style.padBottom = flags.groupLast ? 8.0f : 0.0f;
      // The head of a callout run is its title. It gets no extra height: the
      // `> [!KIND]` line already occupies one, and reserving a band above it
      // as well would leave the box with a blank row over its own name.
      if(block.kind == BlockKind::Callout && flags.groupFirst && block.hasInfo() &&
         !flags.revealed && !flags.raw) {
        out.calloutTitle = true;
        style.base.strong = true;
      }
      break;
    case BlockKind::Code:
      style.base.mono = true;
      style.base.size = type.mono;
      style.padTop = 8.0f;
      style.padBottom = 12.0f;
      break;
    case BlockKind::Divider:
      // A rule needs air on both sides or it reads as an underline.
      style.padTop = 10.0f;
      style.padBottom = 14.0f;
      break;
    case BlockKind::Blank:
      style.padBottom = 0.0f;
      break;
    default:
      break;
  }
  if(flags.raw) {
    // Raw source is source: none of the above applies to it but the air.
    style.base = RunStyle {};
    style.base.mono = true;
    style.base.size = type.mono;
    out.textLeft = out.indent;
  }
  return style;
}

std::vector<Token>& DocumentLayout::nextGroup(std::size_t* count) const {
  if(*count == flowGroups_.size()) flowGroups_.emplace_back();
  std::vector<Token>& group = flowGroups_[(*count)++];
  group.clear();
  return group;
}

std::size_t DocumentLayout::stageSourceLines(const SourceBlock& block, const Flags& flags,
                                             const RunStyle& base) const {
  const std::string_view source = source_;
  const RunStyle markerStyle = base;
  std::size_t groupCount = 0;

  const bool fenced = block.kind == BlockKind::Code && !flags.raw;
  const std::size_t from = fenced ? block.contentStart() : block.start;
  const std::size_t to = fenced ? block.contentEnd() : block.end();
  // Revealed, the opening fence is a line of its own; hidden, it rides in front
  // of the first line of code. Deciding that before the loop rather than
  // splicing it in afterwards is what lets the groups be filled in order.
  if(fenced && flags.revealed) {
    nextGroup(&groupCount)
      .push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                           TextRole::Marker, true, false, -1));
  }
  bool firstLine = true;
  sourceLinesInto(source, from, to, &sourceLines_);
  for(const auto& [lineStart, lineEnd] : sourceLines_) {
    std::vector<Token>& group = nextGroup(&groupCount);
    if(fenced && !flags.revealed && firstLine) {
      group.push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                                TextRole::Marker, true, true, -1));
    }
    firstLine = false;
    if(lineEnd > lineStart) {
      group.push_back(makeToken(source, lineStart, lineEnd, base, TextRole::Code, false, false, -1));
    }
    // The newline itself takes no space but must stay addressable.
    const std::size_t tail = std::min(lineEnd + 1, to);
    if(tail > lineEnd) {
      group.push_back(makeToken(source, lineEnd, tail, base, TextRole::Code, false, true, -1));
    }
  }
  if(fenced && !flags.revealed && firstLine) {
    // `sourceLinesInto` always yields at least one line, so this is unreachable
    // today; it is here so that the opening fence cannot be dropped if it ever
    // yields none.
    nextGroup(&groupCount)
      .push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                           TextRole::Marker, true, true, -1));
  }
  if(fenced && block.end() > block.contentEnd()) {
    Token closing = makeToken(source, block.contentEnd(), block.end(), markerStyle,
                              TextRole::Marker, true, !flags.revealed, -1);
    if(flags.revealed) nextGroup(&groupCount).push_back(std::move(closing));
    else flowGroups_[groupCount - 1].push_back(std::move(closing));
  }
  return groupCount;
}

// The per-byte attribute table for one block's inline spans.
//
// Its own step because it is the one place the *inline* grammar reaches the
// layout: everything the scanner found becomes an attribute on the bytes it
// covers, and everything downstream reads only the table. Fills `out.links` and
// `out.images` on the way, because a span that names a target is the only thing
// that knows the target.
void DocumentLayout::applyInlineSpans(const SourceBlock& block,
                                      const std::vector<SourceSpan>& inlines,
                                      std::vector<Attr>& attrs, BlockLayout& out) const {
  for(const auto& inlineSpan : inlines) {
    // A template rather than a `std::function`: this is called per byte of the
    // span, and through a type-erased call it could not be inlined.
    const auto apply = [&](std::size_t from, std::size_t to, auto&& fn) {
      for(std::size_t i = std::max(from, block.contentStart());
          i < std::min(to, block.contentEnd()); ++i) {
        fn(attrs[i - block.contentStart()]);
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
      case SpanKind::Image: {
        // The alt text becomes the picture's caption -- it is also all a reader
        // gets when the file cannot be drawn -- and the picture itself is
        // reserved under the block, below.
        out.images.push_back({inlineSpan.target, Rect {}});
        out.links.push_back(inlineSpan.target);
        const int link = static_cast<int>(out.links.size()) - 1;
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link](Attr& a) {
          a.link = link;
          a.role = TextRole::ImageAlt;
        });
        break;
      }
      case SpanKind::Link:
      case SpanKind::FootnoteRef:
      case SpanKind::Autolink: {
        out.links.push_back(inlineSpan.target);
        const int link = static_cast<int>(out.links.size()) - 1;
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link](Attr& a) {
          a.link = link;
          a.role = TextRole::Link;
        });
        break;
      }
      case SpanKind::WikiLink: {
        out.links.push_back(inlineSpan.target);
        const int link = static_cast<int>(out.links.size()) - 1;
        const bool resolves =
          !options_.wikiLinkResolves || options_.wikiLinkResolves(inlineSpan.target);
        const auto role = resolves ? TextRole::WikiLink : TextRole::WikiLinkUnresolved;
        apply(inlineSpan.contentStart, inlineSpan.contentEnd, [link, role](Attr& a) {
          a.link = link;
          a.role = role;
        });
        break;
      }
      case SpanKind::Escape:
        break;
    }
  }
}

std::size_t DocumentLayout::stageInlineContent(const SourceBlock& block, const Flags& flags,
                                               const RunStyle& base, BlockLayout& out) const {
  const std::string_view source = source_;
  const RunStyle markerStyle = base;
  std::size_t groupCount = 0;
  std::vector<Token>& group = nextGroup(&groupCount);

  if(block.contentStart() > block.start) {
    group.push_back(makeToken(source, block.start, block.contentStart(), markerStyle,
                              TextRole::Marker, true, !flags.revealed, -1));
  }
  if(block.contentEnd() > block.contentStart()) {
    const perf::ScopeTimer inlineTimer("layout.block.inline_attrs");
    const std::size_t span = block.contentEnd() - block.contentStart();
    const auto& inlines = scanInlinesInto(source.substr(block.contentStart(), span),
                                          block.contentStart(), &inlineScratch_);
    perf::addCounter(perf::CounterId::LayoutInlineSpans, inlines.size());
    if(inlines.empty()) {
      // Nothing marked up, so there is nothing an attribute table could say.
      perf::addCounter(perf::CounterId::LayoutPlainBlocks);
      appendPlainTokens(source, block.contentStart(), block.contentEnd(), base, group);
    } else {
      perf::addCounter(perf::CounterId::LayoutAttrBytes, span);
      // Reassigned rather than reallocated, same as the scan's own buffers: one
      // allocation for a document instead of one per marked-up block.
      std::vector<Attr>& attrs = attrs_;
      attrs.assign(span, Attr {});
      applyInlineSpans(block, inlines, attrs, out);
      {
        const perf::ScopeTimer tokenTimer("layout.block.content_tokens");
        appendContentTokens(source, block.contentStart(), block.contentEnd(), attrs, base,
                            options_.type.mono, flags.revealed, group);
      }
    }
  }
  if(block.end() > block.contentEnd()) {
    // The trailing newline is always zero width: it must never push the line.
    group.push_back(makeToken(source, block.contentEnd(), block.end(), markerStyle,
                              TextRole::Marker, true, true, -1));
  }
  return groupCount;
}

BlockLayout DocumentLayout::layoutBlock(std::size_t index, const Flags& flags) const {
  const perf::ScopeTimer blockTimer("layout.block");
  const SourceBlock& block = blocks_[index];

  BlockLayout out;
  out.kind = block.kind;
  out.revealed = flags.revealed;
  out.raw = flags.raw;

  const BlockStyle style = styleForBlock(block, flags, out);
  const RunStyle& base = style.base;
  const float available = std::max(40.0f, options_.width - out.textLeft);
  const float lineHeight = metrics_.lineHeight ? metrics_.lineHeight(base)
                                               : base.size * options_.type.lineHeightRatio;
  const auto appendTrailingLine = [&](float y) {
    VisualLine line;
    line.y = y;
    line.height = lineHeight;
    line.runBegin = static_cast<std::uint32_t>(out.runs.size());
    TextRun& run = out.runs.emplace_back();
    run.srcStart = block.end() - block.start;
    run.srcEnd = run.srcStart;
    run.rect = {out.textLeft, 0.0f, 0.0f, lineHeight};
    run.style = base;
    line.runEnd = static_cast<std::uint32_t>(out.runs.size());
    out.lines.push_back(line);
    return y + lineHeight;
  };

  // A collapsed block gives up its height and nothing else. The one thing it
  // may not give up is the empty last line: that is the only caret position at
  // the end of the buffer, and losing it would strand the caret.
  if(flags.hidden) {
    out.hidden = true;
    out.height = flags.trailingLine ? appendTrailingLine(0.0f) : 0.0f;
    return out;
  }

  // A block the scanner does not model reserves the height md4c will need, and
  // exposes one caret position at its start until the user drops it to raw.
  if(block.kind == BlockKind::Complex && !flags.raw) {
    out.complex = true;
    const float height =
      metrics_.measureComplex ? metrics_.measureComplex(block, options_.width) : lineHeight;
    VisualLine line;
    line.y = style.padTop;
    line.height = std::max(lineHeight, height);
    line.runBegin = static_cast<std::uint32_t>(out.runs.size());
    TextRun& run = out.runs.emplace_back();
    run.srcStart = 0;
    run.srcEnd = block.end() - block.start;
    run.rect = {out.textLeft, 0.0f, 0.0f, line.height};
    run.style = base;
    run.role = TextRole::Body;
    line.runEnd = static_cast<std::uint32_t>(out.runs.size());
    out.lines.push_back(line);
    float bottom = style.padTop + std::max(lineHeight, height);
    if(flags.trailingLine) bottom = appendTrailingLine(bottom);
    out.height = bottom + style.padBottom;
    return out;
  }

  // The block's content, staged into `flowGroups_` as one group per line's
  // worth of source. Two shapes and no third: a fenced code block or a block
  // dropped to raw is the file's own lines, and everything else is one group
  // with the inline grammar applied to it.
  const bool asSourceLines = flags.raw || block.kind == BlockKind::Code;
  const std::size_t groupCount = asSourceLines ? stageSourceLines(block, flags, base)
                                               : stageInlineContent(block, flags, base, out);

  reserveFlowOutput(block, flags, base, available, groupCount, out);

  float bottom = 0.0f;
  {
    const perf::ScopeTimer flowTimer("layout.block.flow");
    Flow flow(metrics_, block.start, out.textLeft, available, lineHeight,
              !flags.raw && block.kind != BlockKind::Code, style.padTop, out, flowPending_,
              flowCluster_);
    flow.run(flowGroups_, groupCount);
    bottom = flow.bottom();
  }
  if(flags.trailingLine) bottom = appendTrailingLine(bottom);
  // The pictures the block named, under its text and in source order. Reserved
  // here rather than drawn over the following blocks, so the note scrolls past
  // an image the same way it scrolls past a paragraph and every position query
  // below this block is already right.
  if(!out.images.empty()) bottom = placeImages(out, bottom);
  out.height = bottom + style.padBottom;
  return out;
}

// Roughly how many lines this is about to wrap into, so the line vector grows
// once rather than doubling its way there. Half the type size is a crude mean
// glyph advance, and being wrong only costs the doubling this avoids.
void DocumentLayout::reserveFlowOutput(const SourceBlock& block, const Flags& flags,
                                       const RunStyle& base, float available,
                                       std::size_t groupCount, BlockLayout& out) const {
  const std::size_t contentBytes =
    block.contentEnd() > block.contentStart() ? block.contentEnd() - block.contentStart() : 0;
  const float inkWidth = static_cast<float>(contentBytes) * base.size * 0.5f;
  out.lines.reserve(static_cast<std::size_t>(inkWidth / std::max(1.0f, available)) + 1);
  // And exactly how many runs, which is not an estimate: the flow emits one run
  // per staged token, plus one more for a trailing line. A word split across a
  // wrap emits more, which is the one case this under-counts and the one case
  // the doubling is right for.
  std::size_t tokens = flags.trailingLine ? 1 : 0;
  for(std::size_t g = 0; g < groupCount; ++g) tokens += flowGroups_[g].size();
  perf::addCounter(perf::CounterId::LayoutTokensStaged, tokens);
  out.runs.reserve(tokens);
}

// The images of a block that has some, laid out down the column from `top`.
// Split out because a block with none -- which is almost every block -- should
// not pay a branch inside a loop for it.
float DocumentLayout::placeImages(BlockLayout& out, float top) const {
  const float column = std::max(40.0f, options_.width - out.indent);
  for(auto& image : out.images) {
    perf::addCounter(perf::CounterId::LayoutImagesMeasured);
    const ImageBox box = metrics_.measureImage
                           ? metrics_.measureImage(image.target, column, options_.imageMaxHeight)
                           : ImageBox {};
    if(box.width <= 0.0f || box.height <= 0.0f) {
      image.rect = {out.indent, top, 0.0f, 0.0f};
      continue;
    }
    image.rect = {out.indent, top + kImageGap, box.width, box.height};
    top = image.rect.y + box.height + kImageGap;
  }
  return top;
}

}
