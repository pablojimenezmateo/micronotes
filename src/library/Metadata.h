#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace micronotes::library {

struct NoteMetadata {
  std::string id;
  std::string title;
  std::vector<std::string> tags;
};

std::string generateNoteId();
std::string metadataHeader(const NoteMetadata& metadata);
NoteMetadata parseMetadata(std::string_view markdown);
std::string stripMetadataHeader(std::string_view markdown);

}
