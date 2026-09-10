#pragma once

#include <SDL3/SDL_pixels.h>

#include <string>
#include <string_view>

namespace microcore::pdf {

class PdfFont;

// A page's content stream, built in the coordinates everything above it
// already uses.
//
// **A PDF's origin is the bottom left corner and its y grows upward.** Every
// layout in this repository has y growing down from the top, and converting
// between them at each call site is the kind of arithmetic that is right in
// nine places and inverted in the tenth. So this class takes the page height
// once and every coordinate handed to it is measured *from the top*: a rect's
// `y` is its top edge, and a line of text's `y` is its baseline's distance
// from the top of the page. The flip happens here, in one expression.
//
// Colours are `SDL_Color` because that is what the theme is made of and a
// second colour type would only exist to be converted to.
class PdfContent {
public:
  explicit PdfContent(float pageHeight) : pageHeight_(pageHeight) {}

  void save();
  void restore();
  // Clips everything after it to `rect`, until the next `restore`. Used for
  // the same thing `ui::ClipGuard` is: keeping a code block's long line inside
  // its own column.
  void clip(float x, float y, float w, float h);

  void fillRect(float x, float y, float w, float h, SDL_Color color);
  // A filled disc. There is no circle operator in a content stream, so this is
  // four Bezier arcs -- which is worth a function rather than a call site,
  // because the arc constant is the kind of number nobody recognises as wrong.
  void fillCircle(float cx, float cy, float radius, SDL_Color color);
  // A hairline. `thickness` is in points, and a rule thinner than about a
  // quarter point disappears at print resolution.
  void line(float x1, float y1, float x2, float y2, float thickness, SDL_Color color);
  void strokeRect(float x, float y, float w, float h, float thickness, SDL_Color color);

  // One run of text on one line. `y` is the baseline.
  void text(PdfFont& font, int fontSlot, float size, float x, float y, std::string_view value,
            SDL_Color color);

  // An image already registered with the document, drawn into the box given.
  void image(int imageSlot, float x, float y, float w, float h);

  const std::string& bytes() const { return out_; }
  bool empty() const { return out_.empty(); }

private:
  // Top-down y to the PDF's bottom-up y. The one place the two disagree.
  float flip(float y) const { return pageHeight_ - y; }
  void setFill(SDL_Color color);
  void setStroke(SDL_Color color);

  std::string out_;
  float pageHeight_ = 0.0f;
  // The colours currently set in the stream, so a paragraph of runs in one ink
  // emits one `rg` rather than one per word. `-1` means nothing has been set
  // yet, which no colour compares equal to.
  int fillR_ = -1, fillG_ = -1, fillB_ = -1;
  int strokeR_ = -1, strokeG_ = -1, strokeB_ = -1;
};

}
