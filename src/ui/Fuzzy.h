#pragma once

#include <optional>
#include <string_view>

namespace micronotes::ui {

// Case-insensitive subsequence match used by the command palette, the slash
// menu, and any filterable list overlay.
//
// Returns nullopt when every character of `query` cannot be found in order.
// Higher scores are better: consecutive runs and matches at word boundaries
// score above scattered ones, and shorter candidates break ties.
std::optional<int> fuzzyScore(std::string_view text, std::string_view query);

}
