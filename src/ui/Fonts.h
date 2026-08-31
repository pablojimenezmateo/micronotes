#pragma once

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include <string_view>

namespace micronotes::ui {

enum class FontFamily {
  Sans,
  Mono
};

struct TextStyle {
  FontFamily family = FontFamily::Sans;
  bool strong = false;
  bool italic = false;
  // Logical pixels before display scaling. Zero means "body size".
  float size = 0.0f;
};

// Notion-like type scale, in logical pixels.
struct TypeScale {
  float pageTitle = 40.0f;
  float h1 = 30.0f;
  float h2 = 24.0f;
  float h3 = 20.0f;
  float h4 = 17.0f;
  float body = 16.0f;
  float ui = 14.0f;
  float small = 13.0f;
  float tiny = 11.0f;
  float mono = 14.0f;
  float lineHeightRatio = 1.5f;
};

const TypeScale& type();

// Size for a heading of the given Markdown level (1-6).
float headingSize(int level);

// Owns every font face and hides SDL_ttf from the rest of the application.
class FontStore {
public:
  FontStore() = default;
  ~FontStore();
  FontStore(const FontStore&) = delete;
  FontStore& operator=(const FontStore&) = delete;

  bool init();
  void shutdown();
  bool ready() const;

  // Rebuilds every face when the scale actually changes.
  void setDisplayScale(float scale);
  float displayScale() const;

  bool measure(std::string_view value, const TextStyle& style, int* width, int* height) const;
  SDL_Surface* render(std::string_view value, const TextStyle& style, SDL_Color color) const;
  int lineHeight(const TextStyle& style) const;

  // A single emoji rendered from the emoji face at whatever size that face can
  // produce, for the caller to scale into the space it has. Colour emoji fonts
  // carry one fixed bitmap strike that SDL_ttf cannot resize, which is why they
  // are not attached as a fallback for ordinary text and why the size asked for
  // here is only a hint. Null when no emoji face is installed, so the caller
  // can draw its own mark rather than a tofu box.
  bool hasIconFont() const;
  SDL_Surface* renderIcon(std::string_view value, SDL_Color color) const;

  // Which font directory was used, for diagnostics.
  const char* sourceDescription() const;

private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}
