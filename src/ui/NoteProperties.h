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
  // A scalar value, with a multi-line one flattened onto this line.
  std::string value;
};

// The rows for `metadata`, in the order the file carried them.
//
// `id`, `title`, `icon` and `tags` are deliberately absent. They are the note's
// identity rather than facts about it, and the shell already shows all of them
// somewhere better: the title as the title, the icon beside it, the tags as
// coloured dots on the breadcrumb over the note, and the id nowhere, because a
// generated key nobody typed is not something a reader needs on the page.
//
// `tags` was here, as a row of chips above the first line of every tagged note,
// and it was the loudest thing on the page: a band of ground and a coloured dot
// per tag, ahead of the note's own text, restating what the breadcrumb one line
// higher now says in eight pixels.
std::vector<NoteProperty> notePropertiesOf(const library::NoteMetadata& metadata);

}
