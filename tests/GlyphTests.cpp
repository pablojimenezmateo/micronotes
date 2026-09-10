#include "TestSupport.h"

#include "ui/Glyphs.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <set>
#include <string>

// The note marks: `ui/Glyphs.h`'s `noteGlyphs()` table and `drawNoteGlyph`.
//
// There were no tests over any of this, which is how it came to be two lists
// of the same eleven marks -- the table the picker is built from, and an
// `if(id == ...)` chain that drew them -- with nothing holding them level. The
// table carries the drawing function now, and these are what say so: a mark
// that is offered has to *draw*, and a mark that draws has to be offered.

namespace {

// A renderer with no window: an ARGB surface and SDL's software renderer over
// it. Enough for a mark, which is lines and filled rects, and it means these
// can run in the ordinary test binary rather than needing a display.
class Canvas {
public:
  explicit Canvas(int side) : surface_(SDL_CreateSurface(side, side, SDL_PIXELFORMAT_ARGB8888)) {
    micronotes::tests::require(surface_ != nullptr, "could not make a surface to draw on");
    renderer_ = SDL_CreateSoftwareRenderer(surface_);
    micronotes::tests::require(renderer_ != nullptr, "could not make a software renderer");
  }
  ~Canvas() {
    if(renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
    if(surface_ != nullptr) SDL_DestroySurface(surface_);
  }
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;

  SDL_Renderer* renderer() { return renderer_; }

  void clear() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
  }

  // How many pixels are not the ground it was cleared to.
  int inkedPixels() {
    SDL_RenderPresent(renderer_);
    int inked = 0;
    const auto* pixels = static_cast<const std::uint32_t*>(surface_->pixels);
    const int stride = surface_->pitch / 4;
    for(int y = 0; y < surface_->h; ++y) {
      for(int x = 0; x < surface_->w; ++x) {
        if((pixels[y * stride + x] & 0x00FFFFFFu) != 0) ++inked;
      }
    }
    return inked;
  }

private:
  SDL_Surface* surface_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
};

}

// Every mark the picker offers puts ink on the canvas.
//
// This is the half that a table and a chain could never have: a mark added to
// the table without an arm in the chain compiled, appeared in the picker as an
// empty cell, and failed nothing. Here it cannot be added without a function,
// and the function has to actually draw.
MICRONOTES_TEST(every_note_glyph_offered_draws_something) {
  Canvas canvas(32);
  for(const auto& glyph : micronotes::ui::noteGlyphs()) {
    micronotes::tests::require(!glyph.id.empty(), "a note glyph has no id");
    micronotes::tests::require(!glyph.label.empty(),
                               "note glyph '" + std::string(glyph.id) + "' has no label");
    micronotes::tests::require(glyph.draw != nullptr,
                               "note glyph '" + std::string(glyph.id) + "' has nothing to draw it");

    canvas.clear();
    const bool drawn = micronotes::ui::drawNoteGlyph(canvas.renderer(), glyph.id,
                                                     {0.0f, 0.0f, 32.0f, 32.0f},
                                                     SDL_Color {255, 255, 255, 255});
    micronotes::tests::require(drawn, "drawNoteGlyph declined '" + std::string(glyph.id) +
                                        "', which the picker offers");
    const int inked = canvas.inkedPixels();
    micronotes::tests::require(inked > 4, "note glyph '" + std::string(glyph.id) + "' drew " +
                                            std::to_string(inked) + " pixels");
  }
}

// Ids are what a note's front matter stores, so two marks sharing one would
// make the second unreachable and the first ambiguous. See `library/Metadata.h`.
MICRONOTES_TEST(note_glyph_ids_are_unique) {
  std::set<std::string> seen;
  for(const auto& glyph : micronotes::ui::noteGlyphs()) {
    micronotes::tests::require(seen.insert(std::string(glyph.id)).second,
                               "two note glyphs share the id '" + std::string(glyph.id) + "'");
  }
}

// An id from a version that offered a different set, and the empty id a note
// with no icon carries. Both have to be declined rather than drawn as
// something else, because the caller's fallback is what puts the plain
// document mark there instead.
MICRONOTES_TEST(drawing_an_unknown_note_glyph_is_declined) {
  Canvas canvas(32);
  canvas.clear();
  const SDL_Color white {255, 255, 255, 255};
  const micronotes::ui::Rect box {0.0f, 0.0f, 32.0f, 32.0f};
  MICRONOTES_REQUIRE(!micronotes::ui::drawNoteGlyph(canvas.renderer(), "", box, white));
  MICRONOTES_REQUIRE(!micronotes::ui::drawNoteGlyph(canvas.renderer(), "sparkle", box, white));
  MICRONOTES_REQUIRE(!micronotes::ui::drawNoteGlyph(canvas.renderer(), "sta", box, white));
  MICRONOTES_REQUIRE(!micronotes::ui::drawNoteGlyph(canvas.renderer(), "starr", box, white));
  MICRONOTES_REQUIRE(canvas.inkedPixels() == 0);
}
