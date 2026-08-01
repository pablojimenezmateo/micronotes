#include "core/platform/DefaultOpener.h"

namespace microcore::platform {

std::vector<std::string> defaultOpenCommand(const std::filesystem::path& path) {
  return {"xdg-open", path.string()};
}

}
