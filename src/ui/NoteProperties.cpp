#include "ui/NoteProperties.h"

#include <string_view>

namespace micronotes::ui {
namespace {

std::string_view trimmedView(std::string_view value) {
  const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
  std::size_t from = 0;
  std::size_t to = value.size();
  while(from < to && space(value[from])) ++from;
  while(to > from && space(value[to - 1])) --to;
  return value.substr(from, to - from);
}

std::string trimmed(std::string_view value) {
  return std::string(trimmedView(value));
}

// Whether a front-matter line continues the key above it rather than opening
// one of its own. The same rule the parser used to gather these lines, applied
// again here to put them back together.
bool continuesValue(std::string_view line) {
  return !line.empty() && (line.front() == ' ' || line.front() == '\t' || line.starts_with("- "));
}

}

std::vector<NoteProperty> notePropertiesOf(const library::NoteMetadata& metadata) {
  std::vector<NoteProperty> rows;
  if(!metadata.tags.empty()) {
    NoteProperty tags;
    tags.key = "tags";
    tags.chips = metadata.tags;
    rows.push_back(std::move(tags));
  }
  for(const auto& line : metadata.extra) {
    if(continuesValue(line)) {
      // A continuation before any key at all is a malformed header, not a row.
      if(rows.empty()) continue;
      // Trimmed before the sequence dash is looked for, not after: the dash of
      // a block item is indented under its key, so testing the raw line for
      // "- " finds it only on the one item that happens to sit at column zero.
      std::string_view piece = trimmedView(line);
      if(piece.starts_with("- ")) piece.remove_prefix(2);
      const std::string item = trimmed(piece);
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
    row.key = trimmed(std::string_view(line).substr(0, colon));
    if(row.key.empty()) continue;
    row.value = trimmed(std::string_view(line).substr(colon + 1));
    rows.push_back(std::move(row));
  }
  return rows;
}

}
