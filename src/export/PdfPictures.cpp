#include "export/PdfPictures.h"

#include "CoreAliases.h"

#include "core/attachments/AttachmentService.h"
#include "core/pdf/PdfImage.h"
#include "doc/LinkTarget.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#endif

namespace micronotes::exporting {
namespace {

using microcore::pdf::PdfImage;

// A screen is read as 96 dots to the inch and a PDF measures in 72nds of one,
// so a pixel is three quarters of a point.
constexpr float kPointsPerPixel = 72.0f / 96.0f;

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if(!in) return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The file's bytes as `DCTDecode` data, if that is what they already are.
//
// Refused for anything but a baseline grey or colour JPEG: `DCTDecode` is not
// defined over progressive scans, and a CMYK JPEG needs a colour conversion
// that guessing at would be worse than not showing the picture. Both fall
// through to the decoder below, which handles them correctly.
bool passThroughJpeg(std::string bytes, PdfImage& out) {
  const auto info = microcore::pdf::readJpegInfo(bytes);
  if(!info) return false;
  if(info->progressive) return false;
  if(info->components != 1 && info->components != 3) return false;
  out.width = info->width;
  out.height = info->height;
  out.components = info->components;
  out.jpeg = true;
  out.data = std::move(bytes);
  return true;
}

#if MICRONOTES_HAS_SDL3_IMAGE

// Decoded to 8-bit samples, plus the soft mask if the picture has one.
//
// `IMG_Load` gives a surface and wants no renderer and no window, which is
// what lets an export run with no display attached at all -- the test suite
// and a headless session both need that.
bool decodeImage(const std::filesystem::path& path, PdfImage& out) {
  SDL_Surface* loaded = IMG_Load(path.c_str());
  if(!loaded) return false;
  // One format, always, rather than a branch per pixel layout: SDL will
  // convert from whatever the file was, and RGBA is the only one that carries
  // the alpha a soft mask is built from.
  SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  if(!rgba) return false;

  const int width = rgba->w;
  const int height = rgba->h;
  if(width <= 0 || height <= 0) {
    SDL_DestroySurface(rgba);
    return false;
  }
  std::string colour;
  std::string alpha;
  colour.resize(static_cast<std::size_t>(width) * height * 3u);
  alpha.resize(static_cast<std::size_t>(width) * height);
  bool transparent = false;
  const auto* pixels = static_cast<const std::uint8_t*>(rgba->pixels);
  for(int y = 0; y < height; ++y) {
    // Rows are `pitch` bytes apart, which is not the same as four times the
    // width: SDL pads them.
    const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * rgba->pitch;
    for(int x = 0; x < width; ++x) {
      const std::size_t at = static_cast<std::size_t>(y) * width + x;
      colour[at * 3 + 0] = static_cast<char>(row[x * 4 + 0]);
      colour[at * 3 + 1] = static_cast<char>(row[x * 4 + 1]);
      colour[at * 3 + 2] = static_cast<char>(row[x * 4 + 2]);
      const std::uint8_t a = row[x * 4 + 3];
      alpha[at] = static_cast<char>(a);
      transparent = transparent || a != 255;
    }
  }
  SDL_DestroySurface(rgba);

  out.width = width;
  out.height = height;
  out.components = 3;
  out.jpeg = false;
  out.data = std::move(colour);
  // A mask of nothing but 255s is a mask that says "show all of it", which is
  // what happens without one. Carrying it would add a second image the size of
  // the first to every opaque screenshot in the note.
  if(transparent) out.alpha = std::move(alpha);
  return true;
}

#else

bool decodeImage(const std::filesystem::path&, PdfImage&) {
  return false;
}

#endif

}

const PdfPictures::Picture& PdfPictures::resolve(std::string_view target) {
  const std::string key(target);
  if(const auto found = resolved_.find(key); found != resolved_.end()) return found->second;

  Picture picture;
  std::filesystem::path path;
  if(!doc::isRemoteTarget(target) && !root_.empty()) {
    try {
      attachments::AttachmentService service;
      auto managed = service.resolveManaged(root_, key);
      if(service.isSupportedImage(managed)) path = std::move(managed);
    } catch(const std::exception&) {
      path.clear();
    }
  }

  PdfImage image;
  bool built = false;
  if(!path.empty()) {
    std::string bytes = readFile(path);
    if(!bytes.empty()) {
      built = passThroughJpeg(std::move(bytes), image) || decodeImage(path, image);
    }
  }
  if(built && image.valid()) {
    // Read before the move, not after it.
    picture.width = static_cast<float>(image.width) * kPointsPerPixel;
    picture.height = static_cast<float>(image.height) * kPointsPerPixel;
    picture.slot = document_->addImage(std::move(image));
  }
  return resolved_.emplace(key, picture).first->second;
}

}
