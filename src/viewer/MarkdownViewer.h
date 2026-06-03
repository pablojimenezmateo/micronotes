#pragma once

#include "markdown/RenderModel.h"

#include <string>

namespace micronotes::viewer {

struct ViewerLayout {
  int width = 0;
  int blockCount = 0;
  int totalHeight = 0;
};

class MarkdownViewer {
public:
  ViewerLayout layout(const markdown::Document& document, int width) const;
};

}
