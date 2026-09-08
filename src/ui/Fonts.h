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

// The type scale, in logical pixels.
//
// Two families, and the split is not decorative. The *chrome* -- the menu bar,
// the tree, the tab strips, the status bar, every panel and every popup -- is
// set in the mono face at one size, which is what makes it read as an IDE's
// shell rather than as a document's furniture: a column of proportional labels
// in a tree has no vertical rhythm, and a strip of tabs set in a text face
// reads as prose. The *page* keeps the proportional face and the heading
// scale, because that is where the prose actually is.
struct TypeScale {
  // Chrome. One size, and the only mono size the shell outside the page uses.
  float chrome = 13.0f;

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

// The face every chrome surface draws in. A function rather than a constant
// because the size moves with the reader's text setting, and a helper rather
// than the four fields written out at each of the thirty-odd call sites --
// which is how the shell came to have chrome in three different faces.
TextStyle chromeStyle();

// The same, for the one row of chrome that wants a second step down: a search
// snippet under its note's title, a caption under a section label.
TextStyle chromeSmallStyle();

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

  // Which font directory was used, for diagnostics.
  const char* sourceDescription() const;

private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}
