#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace microcore::platform {

std::vector<std::string> defaultOpenCommand(const std::filesystem::path& path);

}
