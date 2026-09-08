#include "library/WikiResolve.h"

#include "CoreAliases.h"

#include "core/util/StringUtil.h"
#include "doc/WikiLink.h"

#include <filesystem>
#include <iterator>
#include <limits>
#include <string>

namespace micronotes::library {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

std::string stem(const std::filesystem::path& path) {
  return path.stem().string();
}

// Among equally good matches, the one nearest the root wins: a note filed at
// the top level is the one someone typing a bare title almost always meant.
bool nearerTheRoot(const NoteListItem& candidate, const NoteListItem& best) {
  const auto candidateDepth = std::distance(candidate.path.begin(), candidate.path.end());
  const auto bestDepth = std::distance(best.path.begin(), best.path.end());
  if(candidateDepth != bestDepth) return candidateDepth < bestDepth;
  return candidate.path.string() < best.path.string();
}

}

std::size_t resolveWikiLink(std::string_view rawTarget, const std::vector<NoteListItem>& notes) {
  const auto target = doc::splitWikiTarget(rawTarget);
  if(target.note.empty()) return kNone;
  const std::string wanted = util::toLowerAscii(target.note);

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

  if(const auto hit = search([&](const NoteListItem& note) {
       return note.title == target.note;
     }); hit != kNone) return hit;

  if(const auto hit = search([&](const NoteListItem& note) {
       return util::toLowerAscii(note.title) == wanted;
     }); hit != kNone) return hit;

  if(const auto hit = search([&](const NoteListItem& note) {
       return stem(note.path) == target.note;
     }); hit != kNone) return hit;

  if(const auto hit = search([&](const NoteListItem& note) {
       return util::toLowerAscii(stem(note.path)) == wanted;
     }); hit != kNone) return hit;

  // A target with a slash in it is a path. `.md` is optional, because nobody
  // types it.
  if(target.note.find('/') == std::string::npos) return kNone;
  const std::string withExtension = target.note.ends_with(".md") ? target.note : target.note + ".md";
  return search([&](const NoteListItem& note) {
    return util::toLowerAscii(note.path.generic_string()).ends_with(util::toLowerAscii(withExtension));
  });
}
}
