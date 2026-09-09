#pragma once

#include "TestSupport.h"

#include "core/perf/PerformanceCounters.h"
#include "doc/Layout.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The fixture every `doc::Layout` test measures against.
//
// `LayoutTests.cpp` was 1,878 lines -- the largest file in the repository --
// holding forty-two tests of three different things: what a laid-out block
// looks like, what the incremental update reuses, and what the queries over the
// result answer. They share only this: a stub face, one fixture note, and two
// helpers about offsets. So this is what they share, and the three subjects are
// three files.
namespace micronotes::tests {

using doc::BlockKind;
using doc::DocumentLayout;
using doc::LayoutOptions;
using doc::Metrics;
using doc::Rect;
using doc::RunStyle;


// A stub font: every codepoint is the same width, so measurement is additive
// and offset round-tripping is exact.
inline float stubMeasure(std::string_view value, const doc::RunStyle& style) {
  std::size_t glyphs = 0;
  for(const char c : value) {
    if((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
  }
  return static_cast<float>(glyphs) * style.size * 0.6f;
}

inline doc::Metrics stubMetrics() {
  doc::Metrics metrics;
  metrics.measure = stubMeasure;
  metrics.lineHeight = [](const doc::RunStyle& style) { return std::round(style.size * 1.5f); };
  return metrics;
}

inline const char* kFixture =
  "# Live surface\n"
  "\n"
  "A paragraph with **strong**, *soft*, `code`, ~~gone~~ and a [link](docs/a.md)\n"
  "that wraps across two source lines and is long enough to wrap on screen too.\n"
  "\n"
  "## Lists\n"
  "\n"
  "- bullet one\n"
  "- an item hand-wrapped across source lines, whose newline and the\n"
  "  indentation continuing it fold into one space\n"
  "- [ ] a task\n"
  "  - nested bullet with rather a lot of words in it to force a visual wrap\n"
  "1. ordered one\n"
  "\n"
  "> quoted text\n"
  "\n"
  "```cpp\n"
  "int main() { return 0; }\n"
  "\n"
  "```\n"
  "\n"
  "---\n"
  "\n"
  "Final caf\xc3\xa9 paragraph.\n";


inline bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// A line ending inside a block - the newline plus the indentation of the line
// continuing it - is drawn as the one space the file means, so the offsets
// inside it share a position and clicking there can only reach the last of
// them. Everywhere else the round trip is the identity; this says exactly where
// it is not, rather than letting the assertion go soft.
inline bool insideOneFoldedLineEnding(const std::string& text, std::size_t a, std::size_t b) {
  if(a == b) return true;
  std::size_t lo = a < b ? a : b;
  std::size_t hi = a < b ? b : a;
  for(std::size_t i = lo; i < hi; ++i) {
    if(!isSpace(text[i])) return false;
  }
  while(lo > 0 && isSpace(text[lo - 1])) --lo;
  while(hi < text.size() && isSpace(text[hi])) ++hi;
  return text.find('\n', lo) < hi;
}

inline std::size_t nextBoundary(const std::string& text, std::size_t index) {
  std::size_t next = index + 1;
  while(next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
  return next;
}


// A document long enough that walking it and walking the viewport are visibly
// different numbers.
inline std::string manyBlocks(int sections) {
  std::string out;
  for(int i = 0; i < sections; ++i) {
    out += "## Section " + std::to_string(i) + "\n\n";
    out += "A paragraph with enough words in it to occupy a line of its own.\n\n";
    out += "- bullet " + std::to_string(i) + "\n\n";
  }
  return out;
}

inline std::uint64_t counter(microcore::perf::CounterId id) {
  return microcore::perf::readCounter(id);
}

// One fenced block thousands of rows long: the shape that made `caretRect` walk
// row by row. It is a single `SourceBlock`, so no amount of block-level
// searching helps -- the search has to be inside the block's runs.
inline std::string oneHugeFence(int lines) {
  std::string out = "```cpp\n";
  for(int i = 0; i < lines; ++i) out += "  int value_" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  out += "```\n";
  return out;
}

// What `caretRect` used to do: walk every row of the caret's block and every
// run of every row, taking the first run that contains the offset, else the
// last run that ended before it, else the block's first row. Kept here as the
// reference the binary search is checked against.
inline Rect caretRectByWalking(const DocumentLayout& layout, std::size_t offset) {
  const LayoutOptions& options = layout.options();
  Rect rect {0.0f, 0.0f, 2.0f, options.type.body * options.type.lineHeightRatio};
  if(layout.blockCount() == 0) return rect;
  std::size_t blockIndex = 0;
  while(blockIndex + 1 < layout.blockCount() && layout.blocks()[blockIndex + 1].start <= offset) {
    ++blockIndex;
  }
  const auto& block = layout.layout(blockIndex);
  if(block.lines.empty()) return rect;
  const float top = layout.blockTop(blockIndex);
  const std::size_t local = offset - layout.blocks()[blockIndex].start;

  const micronotes::doc::TextRun* best = nullptr;
  const micronotes::doc::VisualLine* bestLine = nullptr;
  const micronotes::doc::TextRun* before = nullptr;
  const micronotes::doc::VisualLine* beforeLine = nullptr;
  for(const auto& line : block.lines) {
    for(const auto& run : block.runsOf(line)) {
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
    rect.x = block.textLeft;
    rect.y = top + block.lines.front().y;
    rect.h = block.lines.front().height;
    return rect;
  }
  float x = best->rect.x;
  if(local > best->srcStart && !best->text.empty()) {
    const std::size_t take = std::min(local - best->srcStart, best->text.size());
    x += stubMeasure(std::string_view(best->text).substr(0, take), best->style);
  } else if(local >= best->srcEnd) {
    x = best->rect.x + best->rect.w;
  }
  rect.x = x;
  rect.y = top + bestLine->y;
  rect.h = bestLine->height;
  return rect;
}



// Everything about a laid-out document that anything downstream can observe:
// where each block sits, how tall it is, and every run on every line of it.
// Two layouts that agree here are interchangeable to the view, the caret and
// the hit tester.
inline bool layoutsAgree(const DocumentLayout& a, const DocumentLayout& b, std::string* why) {
  const auto fail = [&](const std::string& message) {
    if(why) *why = message;
    return false;
  };
  if(a.blockCount() != b.blockCount()) return fail("block count");
  for(std::size_t i = 0; i < a.blockCount(); ++i) {
    const auto& left = a.layout(i);
    const auto& right = b.layout(i);
    const std::string at = " at block " + std::to_string(i);
    if(std::abs(a.blockTop(i) - b.blockTop(i)) > 0.001f) return fail("block top" + at);
    if(a.blockHidden(i) != b.blockHidden(i)) return fail("hidden" + at);
    if(left.kind != right.kind) return fail("kind" + at);
    if(std::abs(left.height - right.height) > 0.001f) return fail("height" + at);
    if(std::abs(left.indent - right.indent) > 0.001f) return fail("indent" + at);
    if(std::abs(left.textLeft - right.textLeft) > 0.001f) return fail("text left" + at);
    if(left.revealed != right.revealed) return fail("revealed" + at);
    if(left.raw != right.raw) return fail("raw" + at);
    if(left.complex != right.complex) return fail("complex" + at);
    if(left.calloutTitle != right.calloutTitle) return fail("callout title" + at);
    if(left.links != right.links) return fail("links" + at);
    if(left.lines.size() != right.lines.size()) return fail("line count" + at);
    for(std::size_t l = 0; l < left.lines.size(); ++l) {
      const auto& leftLine = left.lines[l];
      const auto& rightLine = right.lines[l];
      const std::string on = at + " line " + std::to_string(l);
      if(std::abs(leftLine.y - rightLine.y) > 0.001f) return fail("line y" + on);
      if(std::abs(leftLine.height - rightLine.height) > 0.001f) return fail("line height" + on);
      const auto leftRuns = left.runsOf(leftLine);
      const auto rightRuns = right.runsOf(rightLine);
      if(leftRuns.size() != rightRuns.size()) return fail("run count" + on);
      for(std::size_t r = 0; r < leftRuns.size(); ++r) {
        const auto& leftRun = leftRuns[r];
        const auto& rightRun = rightRuns[r];
        const std::string in = on + " run " + std::to_string(r);
        if(leftRun.srcStart != rightRun.srcStart) return fail("run start" + in);
        if(leftRun.srcEnd != rightRun.srcEnd) return fail("run end" + in);
        if(leftRun.text != rightRun.text) return fail("run text" + in);
        if(leftRun.role != rightRun.role) return fail("run role" + in);
        if(leftRun.isMarker != rightRun.isMarker) return fail("run marker" + in);
        if(leftRun.linkIndex != rightRun.linkIndex) return fail("run link" + in);
        if(!(leftRun.style == rightRun.style)) return fail("run style" + in);
        if(std::abs(leftRun.rect.x - rightRun.rect.x) > 0.001f) return fail("run x" + in);
        if(std::abs(leftRun.rect.w - rightRun.rect.w) > 0.001f) return fail("run w" + in);
      }
    }
  }

  // ...and every query the surface actually asks, because the placement is
  // patched in place now rather than rebuilt, and a patch can leave a *correct*
  // block sitting at a stale row. The per-block loop above cannot see that: the
  // row index is a separate array, and nothing in `layout(i)` reads it. These
  // are the readers of it.
  const std::string_view text = a.source();
  const float height = a.totalHeight();
  const std::size_t rowStep = text.size() / 64 + 1;
  for(std::size_t offset = 0; offset <= text.size(); offset += rowStep) {
    const std::string at = " at offset " + std::to_string(offset);
    if(a.rowRelative(offset, 1) != b.rowRelative(offset, 1)) return fail("row down" + at);
    if(a.rowRelative(offset, -1) != b.rowRelative(offset, -1)) return fail("row up" + at);
    if(a.rowRelative(offset, 9) != b.rowRelative(offset, 9)) return fail("row down nine" + at);
    const Rect left = a.caretRect(offset);
    const Rect right = b.caretRect(offset);
    if(std::abs(left.x - right.x) > 0.001f || std::abs(left.y - right.y) > 0.001f ||
       std::abs(left.h - right.h) > 0.001f) {
      return fail("caret rect" + at);
    }
    const auto leftRects = a.selectionRects(offset, offset + rowStep);
    const auto rightRects = b.selectionRects(offset, offset + rowStep);
    if(leftRects.size() != rightRects.size()) return fail("selection rect count" + at);
    for(std::size_t r = 0; r < leftRects.size(); ++r) {
      if(std::abs(leftRects[r].x - rightRects[r].x) > 0.001f ||
         std::abs(leftRects[r].y - rightRects[r].y) > 0.001f ||
         std::abs(leftRects[r].w - rightRects[r].w) > 0.001f ||
         std::abs(leftRects[r].h - rightRects[r].h) > 0.001f) {
        return fail("selection rect" + at);
      }
    }
  }
  const float yStep = height / 48.0f + 1.0f;
  for(float y = -20.0f; y < height + 40.0f; y += yStep) {
    const std::string at = " at y " + std::to_string(y);
    if(a.blockAt(y) != b.blockAt(y)) return fail("block at" + at);
    if(a.blockRange(y, y + 120.0f) != b.blockRange(y, y + 120.0f)) return fail("block range" + at);
    for(const float x : {-10.0f, 0.0f, 37.0f, 240.0f, 4000.0f}) {
      if(a.offsetAt(x, y) != b.offsetAt(x, y)) {
        return fail("offset at x " + std::to_string(x) + at);
      }
    }
  }
  if(a.rowsPerHeight(400.0f) != b.rowsPerHeight(400.0f)) return fail("rows per height");
  if(std::abs(a.totalHeight() - b.totalHeight()) > 0.001f) return fail("total height");
  return true;
}

// A document long enough that the incremental path has a prefix and a suffix to
// carry over, and varied enough that the blocks it carries are not all alike.
inline std::string sectionedFixture(int sections) {
  std::string source;
  for(int section = 0; section < sections; ++section) {
    const std::string n = std::to_string(section);
    source += "## Section " + n + "\n\n";
    source += "A paragraph with **strong text**, `code`, a [link](note-" + n +
              ".md) and enough words in it to wrap more than once on screen.\n\n";
    source += "- bullet " + n + "\n- [ ] task " + n + "\n\n";
    source += "> quoted line " + n + "\n\n";
    source += "```cpp\nint value_" + n + " = " + n + ";\n```\n\n";
    // A table, because whether its first row is a table at all is decided by
    // the line under it. That makes it the one construct here whose kind can
    // change while its own bytes do not, which is what the block-shape check in
    // the reuse map is for.
    source += "| left " + n + " | right |\n|:------|------:|\n| a | b |\n\n";
  }
  return source;
}

inline std::string longFixture() {
  return sectionedFixture(40);
}



// One seeded random walk of edits, asserted against a from-scratch layout after
// every step. Factored out so the test can run a few independent sequences: a
// single seed explores one path through the state machine, and the bugs this is
// built to catch are in the transitions, not in any one state.
inline void walkRandomEdits(std::uint64_t seed, int steps) {
  // xorshift64*, so the sequence is fixed by the seed and does not depend on
  // the standard library's distribution implementations.
  std::uint64_t state = seed;
  const auto next = [&state]() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545F4914F6CDD1Dull;
  };
  const auto pick = [&next](std::size_t bound) {
    return bound == 0 ? std::size_t {0} : static_cast<std::size_t>(next() >> 11) % bound;
  };

  std::string source = sectionedFixture(6);
  DocumentLayout incremental;
  incremental.setMetrics(stubMetrics());

  std::uint64_t revision = 1;
  std::uint64_t foldRevision = 1;
  int foldMode = 0;
  LayoutOptions options;
  options.width = 620.0f;
  options.foldRevision = foldRevision;
  options.folded = [&foldMode](const micronotes::doc::SourceBlock& block) {
    if(foldMode == 0) return false;
    if(block.kind != BlockKind::Heading) return false;
    return foldMode == 1 || block.level == 2;
  };

  int step = 0;
  // The span the caller claims it edited, carried alongside the stamp. It is
  // handed over on every settle, stale or not: the layout is supposed to check
  // it against the buffer it holds and fall back to comparing bytes when the
  // stamps do not line up, and a step that changes the width or the caret
  // rather than the text is exactly that case.
  microcore::editor::TextEdit claim;
  const auto settle = [&](bool stamped) {
    options.sourceRevision = stamped ? revision : 0;
    options.editedSpan = claim;
    incremental.update(source, options);

    DocumentLayout fresh;
    fresh.setMetrics(stubMetrics());
    LayoutOptions freshOptions = options;
    freshOptions.sourceRevision = 0;
    freshOptions.foldRevision = 0;
    fresh.update(source, freshOptions);

    std::string why;
    const bool agree = layoutsAgree(incremental, fresh, &why);
    micronotes::tests::require(agree, "random edit sequence " + std::to_string(seed) +
                                        " diverged at step " + std::to_string(step) + ": " + why);
  };

  settle(true);

  // Snippets rather than random bytes: what stresses the incremental path is an
  // edit that changes a block's *kind* or its extent, and those are markers. A
  // bare "```" is in the list deliberately -- an unclosed fence swallows the
  // rest of the document into one block, which is the largest structural change
  // a three-byte edit can make.
  static const char* kSnippets[] = {"x",  "hello ",   "**bold** ", "# ",     "- ",
                                    "> ", "`code` ",  "[[wiki]] ", "1. ",    "- [ ] ",
                                    "\n", "\n\n",     "```\n",     "|a|b|\n", "    "};
  constexpr std::size_t kSnippetCount = sizeof(kSnippets) / sizeof(kSnippets[0]);

  for(step = 1; step <= steps; ++step) {
    const std::size_t what = pick(20);
    if(what < 7) {
      const std::size_t at = pick(source.size() + 1);
      const std::string_view snippet = kSnippets[pick(kSnippetCount)];
      source.insert(at, snippet);
      claim = {revision, revision + 1, at, at, at + snippet.size()};
      ++revision;
      options.caretOffset = at + snippet.size();
    } else if(what < 11) {
      if(source.empty()) continue;
      const std::size_t at = pick(source.size());
      const std::size_t count = std::min<std::size_t>(pick(12) + 1, source.size() - at);
      source.erase(at, count);
      claim = {revision, revision + 1, at, at + count, at};
      ++revision;
      options.caretOffset = at;
    } else if(what < 15) {
      options.caretOffset = pick(source.size() + 1);
    } else if(what < 16) {
      options.rawOffset = options.rawOffset == DocumentLayout::kNone
                            ? pick(source.size() + 1)
                            : DocumentLayout::kNone;
    } else if(what < 17) {
      foldMode = static_cast<int>(pick(3));
      options.foldRevision = ++foldRevision;
    } else if(what < 18) {
      options.width = 320.0f + static_cast<float>(pick(9)) * 90.0f;
    } else if(what < 19) {
      options.revealAll = !options.revealAll;
    }
    // The remaining draw changes nothing at all, which is the path that has to
    // answer "already correct" without touching the document.

    // Every fourth step drops the stamp, so the byte comparison that stands in
    // for it is exercised against a source the caller says nothing about.
    settle(step % 4 != 0);
  }
}

}
