#include "ui/WikiLink.h"

#include "doc/BlockScan.h"
#include "doc/InlineScan.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace micronotes::ui {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

std::string lowered(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string trimmed(std::string_view value) {
  while(!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while(!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  return std::string(value);
}

std::string stem(const std::filesystem::path& path) {
  return path.stem().string();
}

// Among equally good matches, the one nearest the root wins: a note filed at
// the top level is the one someone typing a bare title almost always meant.
bool nearerTheRoot(const library::NoteListItem& candidate, const library::NoteListItem& best) {
  const auto candidateDepth = std::distance(candidate.path.begin(), candidate.path.end());
  const auto bestDepth = std::distance(best.path.begin(), best.path.end());
  if(candidateDepth != bestDepth) return candidateDepth < bestDepth;
  return candidate.path.string() < best.path.string();
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
  if(hash == std::string_view::npos) return {trimmed(target), {}};
  return {trimmed(target.substr(0, hash)), trimmed(target.substr(hash + 1))};
}

std::size_t resolveWikiLink(std::string_view rawTarget, const std::vector<library::NoteListItem>& notes) {
  const auto target = splitWikiTarget(rawTarget);
  if(target.note.empty()) return kNone;
  const std::string wanted = lowered(target.note);

  // Tried in order, so an exact title always beats a case-insensitive file
  // name. Each rule scans the whole library before the next is tried, or a
  // note early in the list would win on a weaker rule than one later in it.
  const auto search = [&](auto matches) -> std::size_t {
    std::size_t best = kNone;
    for(std::size_t i = 0; i < notes.size(); ++i) {
      if(!matches(notes[i])) continue;
      if(best == kNone || nearerTheRoot(notes[i], notes[best])) best = i;
    }
    return best;
  };

  if(const auto hit = search([&](const library::NoteListItem& note) {
       return note.title == target.note;
     }); hit != kNone) return hit;

  if(const auto hit = search([&](const library::NoteListItem& note) {
       return lowered(note.title) == wanted;
     }); hit != kNone) return hit;

  if(const auto hit = search([&](const library::NoteListItem& note) {
       return stem(note.path) == target.note;
     }); hit != kNone) return hit;

  if(const auto hit = search([&](const library::NoteListItem& note) {
       return lowered(stem(note.path)) == wanted;
     }); hit != kNone) return hit;

  // A target with a slash in it is a path. `.md` is optional, because nobody
  // types it.
  if(target.note.find('/') == std::string::npos) return kNone;
  const std::string withExtension = target.note.ends_with(".md") ? target.note : target.note + ".md";
  return search([&](const library::NoteListItem& note) {
    return lowered(note.path.generic_string()).ends_with(lowered(withExtension));
  });
}

std::string retargetWikiLinks(std::string_view source, std::string_view from, std::string_view to) {
  // Nothing to rewrite in a note that names no note. Both functions here used
  // to answer that by scanning every block of the file and every inline span of
  // every block -- and the overwhelming majority of notes, in any library, carry
  // no `[[` at all. One `memchr`-speed pass settles it instead.
  if(source.find("[[") == std::string_view::npos) return std::string(source);
  std::string out;
  out.reserve(source.size());
  std::size_t copied = 0;
  std::vector<doc::SourceBlock> blocks;
  doc::InlineScratch scratch;
  doc::scanBlocksInto(source, &blocks);
  for(const auto& block : blocks) {
    const auto content = source.substr(block.contentStart, block.contentEnd - block.contentStart);
    for(const auto& span : doc::scanInlinesInto(content, block.contentStart, &scratch)) {
      if(span.kind != doc::SpanKind::WikiLink) continue;
      const auto target = splitWikiTarget(span.target);
      if(target.note != from) continue;
      // Only the note part is replaced. The fragment and the alias are the
      // writer's words about this particular link and are none of a rename's
      // business.
      const std::size_t noteStart = span.openStart + 2;
      out.append(source, copied, noteStart - copied);
      out.append(to);
      copied = noteStart + target.note.size();
    }
  }
  out.append(source, copied, std::string_view::npos);
  return out;
}

std::vector<WikiReference> wikiReferences(std::string_view source) {
  std::vector<WikiReference> references;
  // See `retargetWikiLinks`. This one runs once per note on every index
  // refresh, so the notes with no links in them are the ones it has to be
  // cheap for.
  if(source.find("[[") == std::string_view::npos) return references;
  // One block list and one inline scratch for the whole note. Scanning through
  // the allocating forms grew and dropped three vectors per block, on a path
  // that visits every block of every changed file.
  std::vector<doc::SourceBlock> blocks;
  doc::InlineScratch scratch;
  doc::scanBlocksInto(source, &blocks);
  for(const auto& block : blocks) {
    const auto content = source.substr(block.contentStart, block.contentEnd - block.contentStart);
    for(const auto& span : doc::scanInlinesInto(content, block.contentStart, &scratch)) {
      if(span.kind != doc::SpanKind::WikiLink) continue;
      // The line the link is on, so a backlink can show why it is there.
      const auto lineStart = source.rfind('\n', span.start);
      const auto lineEnd = source.find('\n', span.start);
      const std::size_t from = lineStart == std::string_view::npos ? 0 : lineStart + 1;
      const std::size_t to = lineEnd == std::string_view::npos ? source.size() : lineEnd;
      references.push_back({span.target, trimmed(source.substr(from, to - from))});
    }
  }
  return references;
}

}
