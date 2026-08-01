#pragma once

#include "core/markdown/RenderModel.h"

#include <string_view>

namespace microcore::markdown {

class MarkdownParser {
public:
  Document parse(std::string_view source) const;
};

}
