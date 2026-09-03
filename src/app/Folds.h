#pragma once

#include "app/PageView.h"
#include "app/Shell.h"

#include <cstdint>
#include <string_view>

#include "CoreAliases.h"

namespace micronotes::ui {
class FoldState;
}

namespace micronotes::app {

// The live page's fold predicates.
//
// Both read the *current* note and the current buffer when they are called
// rather than capturing either, so they are installed once for the life of the
// process instead of rebuilt every frame -- two `std::function` assignments a
// frame, each too large for the inline buffer, so two heap allocations and two
// frees for closures whose answers were the only thing that changed.
PageFolds livePageFolds(UiRuntime& ui);

// What the layout's reuse check needs about the folds, per frame.
//
// `anyFolded` is not a convenience. Offering the layout *no* predicate is a
// stronger statement than a predicate that always answers false: with no
// predicate it skips resolving folds over the block list entirely, where a
// predicate has to be called once per foldable block on every edit and the
// all-zero result compared against the all-zero one already held. The page
// keeps the predicate installed and this bool decides whether it is passed on.
struct NoteFoldStamp {
  bool anyFolded = false;
  std::uint64_t stamp = 0;
};

NoteFoldStamp noteFoldStamp(const ui::FoldState& folds, std::string_view noteId);

}
