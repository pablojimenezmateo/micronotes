#include "app/PageImages.h"

#include "app/Shell.h"
#include "core/attachments/AttachmentService.h"
#include "ui/TextUtil.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <string_view>

namespace micronotes::app {
namespace {

// The texture behind an image link, or nothing when there is not one: a remote
// target, a path outside the library, or a file the build cannot decode.
// `ImageCache` answers a repeat ask out of its map, which is what makes this
// cheap enough to call from a measure.
SDL_Texture* imageTexture(ui::ImageCache& images, const UiRuntime& ui, std::string_view target,
                          float& width, float& height) {
  if(ui::isRemoteTarget(target) || !ui.state.hasLibrary()) return nullptr;
  try {
    attachments::AttachmentService service;
    const auto path = service.resolveManaged(ui.state.libraryRoot(), std::string(target));
    if(!service.isSupportedImage(path)) return nullptr;
    return images.load(path, width, height);
  } catch(const std::exception&) {
    return nullptr;
  }
}

// Fitted to the column, to a sane maximum width, and to a share of the page --
// so a note is not one photograph the reader has to scroll past. Rounded here
// rather than at the draw, so the box the layout reserved and the box the
// texture takes are the same number.
doc::ImageBox fitImage(float imageW, float imageH, float column, float maxHeight) {
  doc::ImageBox box;
  if(imageW <= 0.0f || imageH <= 0.0f) return box;
  const float maxW = std::max(40.0f, std::min(column, 720.0f));
  const float maxH = std::max(40.0f, maxHeight);
  const float scale = std::min(std::min(1.0f, maxW / imageW), maxH / imageH);
  box.width = std::round(imageW * scale);
  box.height = std::round(imageH * scale);
  return box;
}

}

void wirePageImages(PageViewHooks& hooks, SDL_Renderer* renderer, ui::ImageCache& images,
                    UiRuntime& ui) {
  hooks.measureImage = [&images, &ui](std::string_view target, float column, float maxHeight) {
    float imageW = 0.0f;
    float imageH = 0.0f;
    if(!imageTexture(images, ui, target, imageW, imageH)) return doc::ImageBox {};
    return fitImage(imageW, imageH, column, maxHeight);
  };
  hooks.drawImage = [renderer, &images, &ui](std::string_view target, ui::Rect box) {
    float imageW = 0.0f;
    float imageH = 0.0f;
    if(SDL_Texture* texture = imageTexture(images, ui, target, imageW, imageH)) {
      const SDL_FRect dst {box.x, box.y, box.w, box.h};
      SDL_RenderTexture(renderer, texture, nullptr, &dst);
    }
  };
}

std::uint64_t pageImageRevision(const ui::ImageCache& images) {
  return images.generation();
}

}
