#include "doc/Layout.h"

#include "doc/Tokenize.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

// Staging: a block's source bytes into the token groups `doc/Flow.h` breaks
// into lines.
//
// The third of the three units a block is laid out by, and the one that had
// stayed behind in `Layout.cpp` while the other two were named -- so that file
// was styling, staging, placement, the cache and the incremental machinery, and
// it grew past the tree's line ceiling. `doc/Tokenize.h` is the split itself
// and this is what decides *what* to hand it: which bytes of the block are
// content, whether they are the file's own lines or marked-up prose, and what
// the inline scanner said about each byte.
//
// Two shapes and no third. A fenced code block or a block dropped to raw is the
// file's own lines, one group each; everything else is one group with the
// inline grammar applied to it.
namespace micronotes::doc {


std::vector<Token>& DocumentLayout::nextGroup(std::size_t* count) const {
  if(*count == flowGroups_.size()) flowGroups_.emplace_back();
  std::vector<Token>& group = flowGroups_[(*count)++];
  group.clear();
  return group;
}

std::size_t DocumentLayout::stageSourceLines(const SourceBlock& block,
                                             const RunStyle& base, const BlockLayout& out) const {
  const std::string_view source = source_;
  const std::string_view display = out.display;
  const std::size_t displayBase = block.start;
  const RunStyle markerStyle = base;
  std::size_t groupCount = 0;

  const bool fenced = block.kind == BlockKind::Code;
  const std::size_t from = fenced ? block.contentStart() : block.start;
  const std::size_t to = fenced ? block.contentEnd() : block.end();
  bool firstLine = true;
  sourceLinesInto(source, from, to, &sourceLines_);
  for(const auto& [lineStart, lineEnd] : sourceLines_) {
    std::vector<Token>& group = nextGroup(&groupCount);
    if(fenced && firstLine) {
      group.push_back(makeToken(display, displayBase, block.start, block.contentStart(), markerStyle,
                                TextRole::Marker, true, true, -1));
    }
    firstLine = false;
    if(lineEnd > lineStart) {
      group.push_back(makeToken(display, displayBase, lineStart, lineEnd, base, TextRole::Code, false, false, -1));
    }
    // The newline itself takes no space but must stay addressable.
    const std::size_t tail = std::min(lineEnd + 1, to);
    if(tail > lineEnd) {
      group.push_back(makeToken(display, displayBase, lineEnd, tail, base, TextRole::Code, false, true, -1));
    }
  }
  if(fenced && firstLine) {
    // `sourceLinesInto` always yields at least one line, so this is unreachable
    // today; it is here so that the opening fence cannot be dropped if it ever
    // yields none.
    nextGroup(&groupCount)
      .push_back(makeToken(display, displayBase, block.start, block.contentStart(), markerStyle,
                           TextRole::Marker, true, true, -1));
  }
  if(fenced && block.end() > block.contentEnd()) {
    // The closing fence takes no width but has to stay addressable, so it
    // rides on the end of the last line rather than claiming one of its own.
    flowGroups_[groupCount - 1]
      .push_back(makeToken(display, displayBase, block.contentEnd(), block.end(), markerStyle,
                           TextRole::Marker, true, true, -1));
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

std::size_t DocumentLayout::stageInlineContent(const SourceBlock& block,
                                               const RunStyle& base, BlockLayout& out) const {
  const std::string_view source = source_;
  const std::string_view display = out.display;
  const std::size_t displayBase = block.start;
  const RunStyle markerStyle = base;
  std::size_t groupCount = 0;
  std::vector<Token>& group = nextGroup(&groupCount);

  if(block.contentStart() > block.start) {
    group.push_back(makeToken(display, displayBase, block.start, block.contentStart(), markerStyle,
                              TextRole::Marker, true, true, -1));
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
      appendPlainTokens(source, display, displayBase, block.contentStart(), block.contentEnd(), base, group);
    } else {
      perf::addCounter(perf::CounterId::LayoutAttrBytes, span);
      // Reassigned rather than reallocated, same as the scan's own buffers: one
      // allocation for a document instead of one per marked-up block.
      std::vector<Attr>& attrs = attrs_;
      attrs.assign(span, Attr {});
      applyInlineSpans(block, inlines, attrs, out);
      {
        const perf::ScopeTimer tokenTimer("layout.block.content_tokens");
        appendContentTokens(source, display, displayBase, block.contentStart(), block.contentEnd(), attrs, base,
                            options_.type.mono, group);
      }
    }
  }
  if(block.end() > block.contentEnd()) {
    // The trailing newline is always zero width: it must never push the line.
    group.push_back(makeToken(display, displayBase, block.contentEnd(), block.end(), markerStyle,
                              TextRole::Marker, true, true, -1));
  }
  return groupCount;
}
}
