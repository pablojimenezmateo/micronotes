#include "doc/WikiLink.h"

#include "CoreAliases.h"

#include "core/util/StringUtil.h"

#include "doc/BlockScan.h"
#include "doc/InlineScan.h"

#include <limits>

namespace micronotes::doc {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

// Visits every `[[wikilink]]` span in `source`, in document order.
//
// Both public walks below need the same two things to be true of it, and both
// used to spell them out for themselves.
//
// The whole-note `[[` probe comes first, because the overwhelming majority of
// notes in any library carry no wikilink at all and scanning every block of
// every one of them to find that out is the cost that matters. One
// `memchr`-speed pass settles it.
//
// And there is one block list and one inline scratch for the whole note:
// scanning through the allocating forms grew and dropped three vectors per
// block, on a path that visits every block of every changed file.
template <typename Visit>
void forEachWikiLink(std::string_view source, const Visit& visit) {
  if(source.find("[[") == std::string_view::npos) return;
  std::vector<SourceBlock> blocks;
  InlineScratch scratch;
  scanBlocksInto(source, &blocks);
  for(const auto& block : blocks) {
    const auto content = source.substr(block.contentStart(), block.contentEnd() - block.contentStart());
    for(const auto& span : scanInlinesInto(content, block.contentStart(), &scratch)) {
      if(span.kind == SpanKind::WikiLink) visit(span);
    }
  }
}

}

std::optional<WikiSpan> findWikiLink(std::string_view text, std::size_t from) {
  // The same rule as `doc::InlineScan`'s wikilink pass, which the header
  // explains: the first `]]` closes, because a `[` inside is someone typing
  // rather than a nested link -- there is no such thing -- and the first `|`
  // splits the target from what to show instead of it.
  for(std::size_t i = from; i + 3 < text.size(); ++i) {
    if(text[i] != '[' || text[i + 1] != '[') continue;
    const std::size_t innerStart = i + 2;
    std::size_t close = std::string_view::npos;
    for(std::size_t scan = innerStart; scan + 1 < text.size(); ++scan) {
      if(text[scan] == ']' && text[scan + 1] == ']') {
        close = scan;
        break;
      }
    }
    if(close == std::string_view::npos) return std::nullopt;
    if(close == innerStart) continue;
    const std::size_t bar = text.substr(innerStart, close - innerStart).find('|');
    WikiSpan span;
    span.start = i;
    span.end = close + 2;
    if(bar == std::string_view::npos) {
      span.target = std::string(text.substr(innerStart, close - innerStart));
      span.label = span.target;
    } else {
      span.target = std::string(text.substr(innerStart, bar));
      span.label = std::string(text.substr(innerStart + bar + 1, close - innerStart - bar - 1));
    }
    // An alias with nothing before the bar has no note to go to, and a label
    // with nothing after it has nothing to draw.
    if(span.target.empty() || span.label.empty()) continue;
    return span;
  }
  return std::nullopt;
}

std::string plainWikiText(std::string_view text) {
  if(text.find("[[") == std::string_view::npos) return std::string(text);
  std::string out;
  out.reserve(text.size());
  std::size_t copied = 0;
  while(const auto span = findWikiLink(text, copied)) {
    out.append(text, copied, span->start - copied);
    out.append(span->label);
    copied = span->end;
  }
  out.append(text, copied, std::string_view::npos);
  return out;
}

WikiTarget splitWikiTarget(std::string_view target) {
  const auto hash = target.find('#');
  if(hash == std::string_view::npos) return {std::string(util::trim(target)), {}};
  return {std::string(util::trim(target.substr(0, hash))),
          std::string(util::trim(target.substr(hash + 1)))};
}

std::string retargetWikiLinks(std::string_view source, std::string_view from, std::string_view to) {
  std::string out;
  out.reserve(source.size());
  std::size_t copied = 0;
  forEachWikiLink(source, [&](const auto& span) {
    const auto target = splitWikiTarget(span.target);
    if(target.note != from) return;
    // Only the note part is replaced. The fragment and the alias are the
    // writer's words about this particular link and are none of a rename's
    // business.
    const std::size_t noteStart = span.openStart + 2;
    out.append(source, copied, noteStart - copied);
    out.append(to);
    copied = noteStart + target.note.size();
  });
  out.append(source, copied, std::string_view::npos);
  return out;
}

std::vector<WikiReference> wikiReferences(std::string_view source) {
  std::vector<WikiReference> references;
  forEachWikiLink(source, [&](const auto& span) {
    // The line the link is on, so a backlink can show why it is there.
    const auto lineStart = source.rfind('\n', span.start);
    const auto lineEnd = source.find('\n', span.start);
    const std::size_t from = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    const std::size_t to = lineEnd == std::string_view::npos ? source.size() : lineEnd;
    references.push_back({span.target, std::string(util::trim(source.substr(from, to - from)))});
  });
  return references;
}

}
