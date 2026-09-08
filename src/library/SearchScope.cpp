#include "library/SearchScope.h"

namespace micronotes::library {

using library::SearchScope;

std::string_view searchScopeLabel(SearchScope scope) {
  switch(scope) {
    case SearchScope::All: return "A";
    case SearchScope::Title: return "T";
    case SearchScope::Content: return "C";
  }
  return "A";
}

std::string_view searchScopeName(SearchScope scope) {
  switch(scope) {
    case SearchScope::All: return "titles and text";
    case SearchScope::Title: return "titles only";
    case SearchScope::Content: return "text only";
  }
  return "titles and text";
}

SearchScope nextSearchScope(SearchScope scope) {
  switch(scope) {
    case SearchScope::All: return SearchScope::Title;
    case SearchScope::Title: return SearchScope::Content;
    case SearchScope::Content: return SearchScope::All;
  }
  return SearchScope::All;
}

}
