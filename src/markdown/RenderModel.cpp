#include "markdown/RenderModel.h"

namespace micronotes::markdown {

std::string plainText(const Document& document) {
  std::string out;
  for(const auto& block : document.blocks) {
    for(const auto& item : block.inlines) {
      out += item.text;
    }
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& item : cell.inlines) out += item.text;
        out.push_back('\t');
      }
      out.push_back('\n');
    }
    out.push_back('\n');
  }
  return out;
}

std::vector<Inline> images(const Document& document) {
  std::vector<Inline> out;
  for(const auto& block : document.blocks) {
    for(const auto& item : block.inlines) {
      if(item.type == InlineType::Image) out.push_back(item);
    }
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& item : cell.inlines) {
          if(item.type == InlineType::Image) out.push_back(item);
        }
      }
    }
  }
  return out;
}

}
