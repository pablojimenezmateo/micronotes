#pragma once

#include "doc/Layout.h"

#include "core/util/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace micronotes::doc {

// Turns tokens into visual lines: the line-breaking half of laying a block out.
//
// It appends to `out.runs` and `out.lines` and reads nothing else about the
// block, which is what makes it separable from the incremental machinery in
// `DocumentLayout` -- and what makes it directly testable, with a `measure`
// that counts characters, over tokens a test writes by hand.
class Flow {
public:
  Flow(const Metrics& metrics, const FlowGeometry& geometry, BlockLayout& out, FlowScratch& scratch);

  // Non-const `groups`: a token's text is moved into the run it becomes rather
  // than copied. Every word in the document was being materialised three times
  // -- once out of the source, once into the token, once into the run -- and
  // the third of those is pure waste, because a token is emitted exactly once
  // and read never again.
  //
  // `count` rather than `groups.size()`: the buffer belongs to the caller and
  // keeps the last block's groups past the live prefix, so that their token
  // storage can be reused rather than freed.
  void run(std::vector<LineGroup>& groups, std::size_t count);

  // The y the block's text ended at, in the same space `FlowGeometry::top` is
  // given in.
  float bottom() const;

private:
  void emit(Token& token, float width);
  void flushPending();
  void placeCluster();
  void pushLine(bool continues = true);
  void splitWord(Token& token);

  const Metrics& metrics_;
  std::size_t base_ = 0;
  float textLeft_ = 0.0f;
  float right_ = 0.0f;
  float lineHeight_ = 0.0f;
  bool wrap_ = true;
  // Whether the line about to be closed began inside a line of the file. See
  // `pushLine`.
  bool continuation_ = false;
  // A line ending has been seen and the spaces it came with are still held.
  // See `run`: the break lands when the next word does.
  bool pendingBreak_ = false;
  float y_ = 0.0f;
  float penX_ = 0.0f;
  // Where the line being built started in `out_.runs`.
  std::uint32_t lineFirstRun_ = 0;
  std::vector<std::pair<std::size_t, float>>& pending_;
  float pendingWidth_ = 0.0f;
  // The cluster's widths, plus the group index its first token sits at: the
  // tokens are contiguous, so nothing else has to be recorded.
  std::vector<float>& cluster_;
  std::size_t clusterBegin_ = 0;
  float clusterWidth_ = 0.0f;
  LineGroup* group_ = nullptr;
  BlockLayout& out_;
};

// --- implementation ---------------------------------------------------------
//
// Inline, in the header, and that is a measurement rather than a preference.
//
// This is the innermost loop of laying a document out: `emit` runs once per
// token and `placeCluster` once per word, and `layoutBlock` calls `run` for
// every block of the note. Release builds carry no LTO, so a definition in a
// `.cpp` of its own is a call the compiler cannot see through -- and the whole
// reason this class was pulled out of `Layout.cpp` was testability, which the
// header gives just as well.
//
// The split form was measured against this one over six interleaved runs of
// the perf harness: every allocation count identical, and the timing deltas
// (-5% to +4%, mixed signs) well inside a run-to-run spread that reaches 49%
// of the minimum on `open.cold_layout`. So the clock could not answer it, and
// the answer that does not need the clock is to leave the hot path's codegen
// exactly as it was. `flow_codegen_is_not_split_across_a_translation_unit`
// in ArchitectureTests is what keeps it that way.

inline Flow::Flow(const Metrics& metrics, const FlowGeometry& geometry, BlockLayout& out,
           FlowScratch& scratch)
    : metrics_(metrics),
      base_(geometry.base),
      textLeft_(geometry.textLeft),
      right_(geometry.textLeft + geometry.width),
      lineHeight_(geometry.lineHeight),
      wrap_(geometry.wrap),
      y_(geometry.top),
      pending_(scratch.pending),
      cluster_(scratch.cluster),
      out_(out) {
  penX_ = textLeft_;
  pending_.clear();
  cluster_.clear();
  lineFirstRun_ = static_cast<std::uint32_t>(out_.runs.size());
}

inline void Flow::run(std::vector<LineGroup>& groups, std::size_t count) {
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
        // A break the writer put there, not one the column forced.
        pushLine(false);
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
    pushLine(false);
    pendingBreak_ = false;
  }
}

inline float Flow::bottom() const {
  return y_;
}

inline void Flow::emit(Token& token, float width) {
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

inline void Flow::flushPending() {
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
inline void Flow::placeCluster() {
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

// `continues` says the line of the *file* runs on past this visual line, which
// makes the next one a continuation. False closes a line the file itself
// ended, and false is what a group boundary and a hard break both pass.
inline void Flow::pushLine(bool continues) {
  // The runs are already in the block's array, in order. Closing a line is
  // recording where it ends -- no vector to allocate, no runs to move, and
  // nothing to free again when the cache drops the block.
  VisualLine line;
  line.y = y_;
  line.height = lineHeight_;
  line.runBegin = lineFirstRun_;
  line.runEnd = static_cast<std::uint32_t>(out_.runs.size());
  line.continuation = continuation_;
  continuation_ = continues;
  lineFirstRun_ = line.runEnd;
  out_.lines.push_back(line);
  y_ += lineHeight_;
  penX_ = textLeft_;
}

// A word wider than the whole column is broken at codepoint boundaries so it
// never disappears past the right edge.
inline void Flow::splitWord(Token& token) {
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

}
