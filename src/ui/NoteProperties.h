#pragma once

#include "library/Metadata.h"

#include <string>
#include <vector>

namespace micronotes::ui {

// A note's front matter, turned into rows a page can draw above its content.
//
// The parser keeps unrecognized keys verbatim, as a flat list of the source
// lines they occupied, so that saving cannot destroy them. That is the right
// shape for writing the header back out and the wrong one for showing it: a
// key whose value is a block sequence arrives as four unrelated strings. This
// puts them back together.
//
// It is data in and data out, with no renderer and no runtime, which is why it
// sits here and not beside the code that draws it: the grouping rule is worth a
// test, and a test cannot open a window.
struct NoteProperty {
  std::string key;
  // A scalar value, with a multi-line one flattened onto this line. Empty when
  // the row carries chips instead.
  std::string value;
  // Tags, which are drawn as chips rather than as a run of text. A row has one
  // or the other, never both.
  std::vector<std::string> chips;
};

// The rows for `metadata`, in the order they should be shown: tags first,
// because they are the key micronotes itself understands, then everything the
// file carried in the order the file carried it.
//
// `id`, `title` and `icon` are deliberately absent. They are the note's
// identity rather than facts about it, and the page already shows all three --
// the title as the title, the icon beside it, and the id nowhere, because a
// generated key nobody typed is not something a reader needs on the page.
std::vector<NoteProperty> notePropertiesOf(const library::NoteMetadata& metadata);

}
