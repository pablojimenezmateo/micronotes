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
  std::string out;
  out.reserve(source.size());
  std::size_t copied = 0;
  for(const auto& block : doc::scanBlocks(source)) {
    const auto content = source.substr(block.contentStart, block.contentEnd - block.contentStart);
    for(const auto& span : doc::scanInlines(content, block.contentStart)) {
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
  for(const auto& block : doc::scanBlocks(source)) {
    const auto content = source.substr(block.contentStart, block.contentEnd - block.contentStart);
    for(const auto& span : doc::scanInlines(content, block.contentStart)) {
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
