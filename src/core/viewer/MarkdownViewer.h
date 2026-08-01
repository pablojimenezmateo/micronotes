#pragma once

#include "core/markdown/RenderModel.h"

#include <string>

namespace microcore::viewer {

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
