#include "app/PageImages.h"

#include "app/Shell.h"
#include "core/attachments/AttachmentService.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/LinkTarget.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace micronotes::app {
namespace {

// Where an image target lands on disk, or an empty path when it lands nowhere
// drawable: a remote target, a path outside the library, a file the build
// cannot decode.
//
// Memoised on the runtime, because resolving one canonicalises the library root
// and the candidate -- a `stat` per path component, twice -- and this is asked
// per picture per relaid block. A note of 200 pictures paid about 3,200
// syscalls for its first layout.
const std::filesystem::path& resolvedImagePath(UiRuntime& ui, std::string_view target) {
  static const std::filesystem::path kNone;
  if(!ui.state.catalog().isOpen()) return kNone;
  const auto& root = ui.state.catalog().root();
  ui.imagePaths.retarget(root);
  if(const auto* found = ui.imagePaths.find(target)) return *found;
  std::filesystem::path path;
  if(!doc::isRemoteTarget(target)) {
    try {
      attachments::AttachmentService service;
      auto managed = service.resolveManaged(root, std::string(target));
      if(service.isSupportedImage(managed)) path = std::move(managed);
    } catch(const std::exception&) {
      path.clear();
    }
  }
  return ui.imagePaths.keep(target, std::move(path));
}

// The texture behind an image link, or nothing when there is not one.
// `ImageCache` answers a repeat ask out of its own map, keyed by the resolved
// path -- so between the two of them a picture already on screen costs two map
// probes rather than a walk of the filesystem.
SDL_Texture* imageTexture(ui::ImageCache& images, UiRuntime& ui, std::string_view target,
                          float& width, float& height) {
  const auto& path = resolvedImagePath(ui, target);
  if(path.empty()) return nullptr;
  return images.load(path, width, height);
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
