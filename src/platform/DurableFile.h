#pragma once

#include <filesystem>
#include <string_view>

namespace micronotes::platform {

bool writeFileDurably(const std::filesystem::path& path, std::string_view contents);
bool removeFileDurably(const std::filesystem::path& path);

}
