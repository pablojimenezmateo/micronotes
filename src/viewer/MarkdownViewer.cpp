#include "viewer/MarkdownViewer.h"

namespace micronotes::viewer {

ViewerLayout MarkdownViewer::layout(const markdown::Document& document, int width) const {
  int totalHeight = 0;
  for(const auto& block : document.blocks) {
    int chars = 0;
    for(const auto& item : block.inlines) chars += static_cast<int>(item.text.size());
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& item : cell.inlines) chars += static_cast<int>(item.text.size());
      }
    }
    const int lines = std::max(1, (chars / std::max(40, width / 9)) + 1);
    totalHeight += block.type == markdown::BlockType::Heading ? 32 : block.type == markdown::BlockType::Table ? 28 * std::max(1, static_cast<int>(block.tableRows.size())) : 20 * lines;
    totalHeight += 8;
  }
  return {width, static_cast<int>(document.blocks.size()), totalHeight};
}

}
