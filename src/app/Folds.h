#pragma once

#include "app/PageView.h"

#include <cstdint>
#include <string>

// `editor::` is an alias of `microcore::editor` rather than a namespace of its
// own, so the editor cannot be forward-declared through it.
#include "CoreAliases.h"
#include "core/editor/MarkdownEditor.h"

namespace micronotes::ui {
class FoldState;
}

namespace micronotes::app {

// What the live page needs to know about one note's collapsed toggles: the
// predicates it asks, and the stamp that says whether the answers have moved
// since the last frame.
struct NoteFolds {
  PageFolds page;
  std::uint64_t stamp = 0;
};

// `collapsed` is deliberately left unset when the note has nothing collapsed.
// That is a stronger statement than a predicate that always answers false: with
// no predicate at all the layout skips resolving folds over the block list
// entirely, where a predicate has to be called once per foldable block on every
// edit and the all-zero result compared against the all-zero one already held.
//
// Both closures hold `folds` and `editor` by reference, and the note id by
// value, so the result must not outlive the frame it was built for -- which is
// also the only frame its stamp describes.
NoteFolds noteFolds(ui::FoldState& folds, const editor::MarkdownEditor& editor,
                    std::string noteId);

}
