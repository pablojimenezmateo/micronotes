#include "viewer/SdlMarkdownRenderer.h"

#include <SDL3/SDL.h>

namespace micronotes::viewer {

void SdlMarkdownRenderer::draw(SDL_Renderer* renderer, const markdown::Document& document, int x, int y, int width) const {
  if(!renderer) return;
  float cursorY = static_cast<float>(y);
  for(const auto& block : document.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const float height = heading ? 28.0f : code ? 34.0f : 20.0f;
    SDL_FRect rect {static_cast<float>(x), cursorY, static_cast<float>(width), height};
    if(heading) {
      SDL_SetRenderDrawColor(renderer, 54, 88, 130, 255);
    } else if(code) {
      SDL_SetRenderDrawColor(renderer, 36, 40, 46, 255);
    } else {
      SDL_SetRenderDrawColor(renderer, 25, 29, 34, 255);
    }
    SDL_RenderFillRect(renderer, &rect);
    cursorY += height + 8.0f;
  }
}

}
