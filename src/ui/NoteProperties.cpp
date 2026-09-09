#include "ui/NoteProperties.h"

#include "CoreAliases.h"

#include "core/util/StringUtil.h"

#include <string_view>

namespace micronotes::ui {

// The two rules the parser split these lines on, applied again here to put them
// back together. Borrowed from `library/Metadata.h` rather than restated: this
// file used to carry its own copy of both, and a copy of a *shape* rule is the
// kind that goes wrong quietly -- the header would still parse and still save,
// and only the rows drawn above the note would be assembled wrongly.
using library::continuesFrontMatterValue;
using library::frontMatterSequenceItem;

std::vector<NoteProperty> notePropertiesOf(const library::NoteMetadata& metadata) {
  std::vector<NoteProperty> rows;
  for(const auto& line : metadata.extra) {
    if(continuesFrontMatterValue(line)) {
      // A continuation before any key at all is a malformed header, not a row.
      if(rows.empty()) continue;
      // A `- item` line contributes its item; an indented scalar contributes
      // the whole trimmed line.
      std::string item = frontMatterSequenceItem(line);
      if(item.empty()) item = std::string(util::trim(line));
      if(item.empty()) continue;
      // Flattened onto one line on purpose. The header is a summary of what the
      // note carries; a page that reserves eleven rows for an eleven-item list
      // has stopped being a header and started being the note.
      auto& value = rows.back().value;
      value += value.empty() ? item : ", " + item;
      continue;
    }
    const auto colon = line.find(':');
    if(colon == std::string::npos) continue;
    NoteProperty row;
    row.key = std::string(util::trim(std::string_view(line).substr(0, colon)));
    if(row.key.empty()) continue;
    row.value = std::string(util::trim(std::string_view(line).substr(colon + 1)));
    rows.push_back(std::move(row));
  }
  return rows;
}

}
