#include "core/pdf/PdfContent.h"

#include "core/pdf/PdfFont.h"
#include "core/pdf/PdfWriter.h"

namespace microcore::pdf {
namespace {

// A colour component as a PDF operand: 0 to 1, not 0 to 255.
std::string component(std::uint8_t value) {
  return number(static_cast<double>(value) / 255.0);
}

std::string slotName(const char* prefix, int slot) {
  return std::string(prefix) + std::to_string(slot);
}

}

void PdfContent::save() {
  out_ += "q\n";
  // The graphics state stack restores colours too, so what this object
  // believes is set has to stop being believed across a save/restore pair.
  fillR_ = fillG_ = fillB_ = -1;
  strokeR_ = strokeG_ = strokeB_ = -1;
}

void PdfContent::restore() {
  out_ += "Q\n";
  fillR_ = fillG_ = fillB_ = -1;
  strokeR_ = strokeG_ = strokeB_ = -1;
}

void PdfContent::setFill(SDL_Color color) {
  if(color.r == fillR_ && color.g == fillG_ && color.b == fillB_) return;
  fillR_ = color.r;
  fillG_ = color.g;
  fillB_ = color.b;
  out_ += component(color.r) + " " + component(color.g) + " " + component(color.b) + " rg\n";
}

void PdfContent::setStroke(SDL_Color color) {
  if(color.r == strokeR_ && color.g == strokeG_ && color.b == strokeB_) return;
  strokeR_ = color.r;
  strokeG_ = color.g;
  strokeB_ = color.b;
  out_ += component(color.r) + " " + component(color.g) + " " + component(color.b) + " RG\n";
}

void PdfContent::clip(float x, float y, float w, float h) {
  if(w <= 0.0f || h <= 0.0f) return;
  out_ += number(x) + " " + number(flip(y + h)) + " " + number(w) + " " + number(h) + " re W n\n";
}

void PdfContent::fillRect(float x, float y, float w, float h, SDL_Color color) {
  if(w <= 0.0f || h <= 0.0f || color.a == 0) return;
  setFill(color);
  out_ += number(x) + " " + number(flip(y + h)) + " " + number(w) + " " + number(h) + " re f\n";
}

void PdfContent::fillCircle(float cx, float cy, float radius, SDL_Color color) {
  if(radius <= 0.0f || color.a == 0) return;
  setFill(color);
  // The distance a control point sits from its arc's end to make a cubic
  // Bezier match a quarter circle: 4/3 * (sqrt(2) - 1).
  const float k = radius * 0.5522847f;
  const float y = flip(cy);
  const auto point = [](float px, float py) { return number(px) + " " + number(py) + " "; };
  out_ += point(cx - radius, y) + "m\n";
  out_ += point(cx - radius, y + k) + point(cx - k, y + radius) + point(cx, y + radius) + "c\n";
  out_ += point(cx + k, y + radius) + point(cx + radius, y + k) + point(cx + radius, y) + "c\n";
  out_ += point(cx + radius, y - k) + point(cx + k, y - radius) + point(cx, y - radius) + "c\n";
  out_ += point(cx - k, y - radius) + point(cx - radius, y - k) + point(cx - radius, y) + "c\n";
  out_ += "f\n";
}

void PdfContent::line(float x1, float y1, float x2, float y2, float thickness, SDL_Color color) {
  if(color.a == 0) return;
  setStroke(color);
  out_ += number(thickness) + " w\n";
  out_ += number(x1) + " " + number(flip(y1)) + " m " + number(x2) + " " + number(flip(y2)) +
          " l S\n";
}

void PdfContent::strokeRect(float x, float y, float w, float h, float thickness, SDL_Color color) {
  if(w <= 0.0f || h <= 0.0f || color.a == 0) return;
  setStroke(color);
  out_ += number(thickness) + " w\n";
  // Inset by half the stroke, so the drawn edge lands inside the rect asked
  // for rather than straddling it -- which is what makes two boxes drawn side
  // by side share a seam of the right weight.
  const float inset = thickness / 2.0f;
  out_ += number(x + inset) + " " + number(flip(y + h - inset)) + " " + number(w - thickness) +
          " " + number(h - thickness) + " re S\n";
}

void PdfContent::text(PdfFont& font, int fontSlot, float size, float x, float y,
                      std::string_view value, SDL_Color color) {
  if(value.empty() || size <= 0.0f || color.a == 0) return;
  setFill(color);
  out_ += "BT ";
  out_ += slotName("/F", fontSlot) + " " + number(size) + " Tf ";
  out_ += number(x) + " " + number(flip(y)) + " Td ";
  out_ += font.encode(value);
  out_ += " TJ ET\n";
}

void PdfContent::image(int imageSlot, float x, float y, float w, float h) {
  if(w <= 0.0f || h <= 0.0f) return;
  // An image is drawn into the unit square, so the matrix *is* the box: width
  // and height on the diagonal, the bottom-left corner in the translation.
  out_ += "q\n";
  out_ += number(w) + " 0 0 " + number(h) + " " + number(x) + " " + number(flip(y + h)) + " cm\n";
  out_ += slotName("/Im", imageSlot) + " Do\nQ\n";
}

}
