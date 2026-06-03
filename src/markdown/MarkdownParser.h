#pragma once

#include "markdown/RenderModel.h"

#include <string_view>

namespace micronotes::markdown {

class MarkdownParser {
public:
  Document parse(std::string_view source) const;
};

}
