#pragma once

#include "core/markdown/RenderModel.h"

struct SDL_Renderer;

namespace microcore::viewer {

class SdlMarkdownRenderer {
public:
  void draw(SDL_Renderer* renderer, const markdown::Document& document, int x, int y, int width) const;
};

}
