#include "ui/Glyphs.h"

#include "ui/Painter.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace micronotes::ui {

namespace {

// The marks a note can wear, in the order the picker lays them out.
//
// Drawn rather than typeset, for the reason every other glyph in this file is:
// the chrome face is a mono programming face with none of these in it, and the
// one face that does carry them -- a colour emoji font -- is a single fixed
// bitmap strike that has to be resampled to any size the shell actually uses.
// A note's icon is drawn at sixteen pixels beside its name, which is exactly
// where a resampled 136px bitmap looks worst.
//
// Eleven of them, and not a hundred: an icon here is a note saying what kind of
// thing it is, and a reader who has to hunt through a grid for the right shade
// of meaning has been handed a worse job than typing the word.
constexpr NoteGlyph kNoteGlyphs[] = {
  {"star", "Star"},         {"check", "Done"},      {"flag", "Follow up"},
  {"bookmark", "Bookmark"}, {"tag", "Label"},       {"folder", "Collection"},
  {"calendar", "Dated"},    {"clock", "Waiting"},   {"bolt", "Urgent"},
  {"warning", "Careful"},   {"code", "Technical"},
};

// A ring, from a polygon with enough sides that the corners are gone at this
// size. Cheaper than the disc in `drawTagDot`, which fills; a clock face wants
// the hole.
void strokeCircle(SDL_Renderer* renderer, float cx, float cy, float r, SDL_Color color) {
  constexpr int kSides = 24;
  SDL_FPoint hull[kSides + 1];
  for(int i = 0; i < kSides; ++i) {
    const float angle = static_cast<float>(i) * 6.2831853f / static_cast<float>(kSides);
    hull[i] = SDL_FPoint {cx + std::cos(angle) * r, cy + std::sin(angle) * r};
  }
  hull[kSides] = hull[0];
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLines(renderer, hull, kSides + 1);
}

// A closed outline through the given points, in the box's own coordinates.
void strokePath(SDL_Renderer* renderer, const SDL_FPoint* points, int count, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLines(renderer, points, count);
}

}

void drawChevron(SDL_Renderer* renderer, float x, float centerY, bool open, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(x);
  const float cy = std::round(centerY);
  if(open) {
    // Pointing down: two strokes meeting below their ends.
    SDL_RenderLine(renderer, cx, cy - 2.0f, cx + 4.0f, cy + 2.0f);
    SDL_RenderLine(renderer, cx + 8.0f, cy - 2.0f, cx + 4.0f, cy + 2.0f);
    return;
  }
  SDL_RenderLine(renderer, cx + 2.0f, cy - 4.0f, cx + 6.0f, cy);
  SDL_RenderLine(renderer, cx + 2.0f, cy + 4.0f, cx + 6.0f, cy);
}

void drawCloseGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  SDL_RenderLine(renderer, cx - 3.0f, cy - 3.0f, cx + 3.0f, cy + 3.0f);
  SDL_RenderLine(renderer, cx + 3.0f, cy - 3.0f, cx - 3.0f, cy + 3.0f);
}

void drawCheckGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  // Two strokes, the short arm down-right and the long one up-right. Doubled a
  // pixel apart so the tick has some weight against a row's ground; a
  // single-pixel tick disappears next to the label beside it.
  for(float d = 0.0f; d <= 1.0f; d += 1.0f) {
    SDL_RenderLine(renderer, cx - 4.0f, cy + d, cx - 1.0f, cy + 3.0f + d);
    SDL_RenderLine(renderer, cx - 1.0f, cy + 3.0f + d, cx + 4.0f, cy - 3.0f + d);
  }
}

void drawResetGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  if(!renderer) return;
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  // Whole pixels: line art whose vertices land on halves is line art drawn
  // twice at half opacity, which at this size reads as a smudge.
  const float cx = std::floor(box.x + box.w * 0.5f);
  const float cy = std::floor(box.y + box.h * 0.5f);
  SDL_RenderLine(renderer, cx - 3.0f, cy - 3.0f, cx + 3.0f, cy - 3.0f);
  SDL_RenderLine(renderer, cx + 3.0f, cy - 3.0f, cx + 3.0f, cy + 3.0f);
  SDL_RenderLine(renderer, cx + 3.0f, cy + 3.0f, cx - 2.0f, cy + 3.0f);
  SDL_RenderLine(renderer, cx - 3.0f, cy - 3.0f, cx - 1.0f, cy - 5.0f);
  SDL_RenderLine(renderer, cx - 3.0f, cy - 3.0f, cx, cy - 1.0f);
}

void drawArrowGlyph(SDL_Renderer* renderer, Rect box, bool pointRight, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  const float arm = std::max(3.0f, box.h * 0.22f);
  const float dx = pointRight ? arm * 0.5f : -arm * 0.5f;
  SDL_RenderLine(renderer, cx - dx, cy - arm, cx + dx, cy);
  SDL_RenderLine(renderer, cx + dx, cy, cx - dx, cy + arm);
}

void drawWrapGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  // A shaft along the lower third pointing left, and a riser at its right end:
  // the line goes on, and it goes on below.
  const float left = std::round(box.x + 1.0f);
  const float right = std::round(box.x + box.w - 1.0f);
  const float shaft = std::round(box.y + box.h * 0.66f);
  const float top = std::round(box.y + box.h * 0.2f);
  SDL_RenderLine(renderer, left, shaft, right, shaft);
  SDL_RenderLine(renderer, right, shaft, right, top);
  // The head, two strokes rather than a filled triangle: at seven pixels a fill
  // is a blob and two lines stay a point.
  const float head = std::max(2.0f, box.h * 0.22f);
  SDL_RenderLine(renderer, left, shaft, left + head, shaft - head);
  SDL_RenderLine(renderer, left, shaft, left + head, shaft + head);
}

void drawSearchGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  // A ring and a handle. The ring is a rounded outline rather than a circle: at
  // twelve pixels the difference is invisible and the outline is exact.
  const float ring = std::max(6.0f, box.w - 4.0f);
  stroke(renderer, {box.x, box.y, ring, ring}, color);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLine(renderer, box.x + ring - 1.0f, box.y + ring - 1.0f, box.x + box.w - 1.0f,
                 box.y + box.h - 1.0f);
}

void drawStarGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color) {
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  const float r = std::max(4.0f, std::min(box.w, box.h) / 2.0f - 1.0f);
  // Five points, alternating outer and inner radius. Built as a polygon and
  // then either scan-filled or stroked, so the two states are the same shape --
  // a filled star and an outline of a different star would read as two marks.
  constexpr int kPoints = 10;
  SDL_FPoint hull[kPoints + 1];
  for(int i = 0; i < kPoints; ++i) {
    const float radius = (i % 2 == 0) ? r : r * 0.42f;
    // Starting at -90 degrees, so a point sits at the top where the eye looks.
    const float angle = -1.5707963f + static_cast<float>(i) * 3.14159265f / 5.0f;
    hull[i] = SDL_FPoint {cx + std::cos(angle) * radius, cy + std::sin(angle) * radius};
  }
  hull[kPoints] = hull[0];

  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  if(!filled) {
    SDL_RenderLines(renderer, hull, kPoints + 1);
    return;
  }
  // Scanline fill, even-odd: for each row, every crossing of the outline in
  // order, filled between the first pair, the second pair, and so on.
  //
  // It used to take only the leftmost and rightmost crossing, which is a
  // convex-hull fill -- and a star is the textbook non-convex polygon, so the
  // two lower points were swallowed and what the favourite mark actually drew
  // was a lump. Sorting the crossings is the whole difference, and a row of a
  // five-pointed star has at most six of them.
  // Three spans a row at the star's waist, over at most the star's own height:
  // a named ceiling rather than the same `192` written at three of the four
  // places that had to agree about it.
  constexpr int kMaxSpans = 192;
  SDL_FRect spans[kMaxSpans];
  int count = 0;
  const int top = static_cast<int>(std::floor(cy - r));
  const int bottom = static_cast<int>(std::ceil(cy + r));
  for(int y = top; y <= bottom && count + 3 < kMaxSpans; ++y) {
    const float row = static_cast<float>(y) + 0.5f;
    float crossings[kPoints];
    int found = 0;
    for(int i = 0; i < kPoints; ++i) {
      const SDL_FPoint a = hull[i];
      const SDL_FPoint b = hull[i + 1];
      if((row < a.y && row < b.y) || (row >= a.y && row >= b.y)) continue;
      const float t = (row - a.y) / (b.y - a.y);
      crossings[found++] = a.x + (b.x - a.x) * t;
    }
    if(found < 2) continue;
    // Insertion sort, not `std::sort`, and the reason is a warning rather than
    // a measurement. `found` is at most six -- the comment above says so -- and
    // libstdc++'s sort has an insertion-sort arm indexed at `first + 16`,
    // guarded by a length test the optimiser cannot fold away against a bound
    // it does not know. So every build reported `array subscript 16 is outside
    // array bounds of float [10]` here: a false positive, on the one path in
    // this file that draws per frame, sitting where a real out-of-bounds
    // warning would have gone unnoticed among it. Six elements do not need a
    // partition either.
    for(int i = 1; i < found; ++i) {
      const float value = crossings[i];
      int j = i - 1;
      while(j >= 0 && crossings[j] > value) {
        crossings[j + 1] = crossings[j];
        --j;
      }
      crossings[j + 1] = value;
    }
    for(int i = 0; i + 1 < found && count < kMaxSpans; i += 2) {
      const float left = crossings[i];
      const float right = crossings[i + 1];
      // A span covering less than half a pixel is dropped rather than rounded
      // up to one. At the very tip of a point the two edges cross inside a
      // single row, and a rounded-up span there lands beside the tip instead of
      // on it -- a loose speck floating off the star, which reads far worse
      // than the blunt point that dropping it leaves.
      if(right - left < 0.5f) continue;
      spans[count++] = SDL_FRect {std::round(left), static_cast<float>(y),
                                  std::max(1.0f, std::round(right - left)), 1.0f};
    }
  }
  if(count > 0) SDL_RenderFillRects(renderer, spans, count);
}

std::span<const NoteGlyph> noteGlyphs() {
  return std::span<const NoteGlyph>(kNoteGlyphs, std::size(kNoteGlyphs));
}

bool drawNoteGlyph(SDL_Renderer* renderer, std::string_view id, Rect box, SDL_Color color) {
  if(id.empty()) return false;
  // A 12x12 field centred in whatever box the caller has, so one set of
  // coordinates below serves a 16px sidebar row and a 30px picker cell alike.
  const float side = 12.0f;
  const float left = std::round(box.x + (box.w - side) / 2.0f);
  const float top = std::round(box.y + (box.h - side) / 2.0f);
  const float cx = left + side / 2.0f;
  const float cy = top + side / 2.0f;
  const Rect field {left, top, side, side};
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

  if(id == "star") {
    drawStarGlyph(renderer, field, true, color);
    return true;
  }
  if(id == "check") {
    drawCheckGlyph(renderer, field, color);
    return true;
  }
  if(id == "flag") {
    // A pole with the pennant hanging off its top half, so the mark has a
    // baseline the way a letter does.
    SDL_RenderLine(renderer, left + 2.0f, top, left + 2.0f, top + side);
    const SDL_FPoint pennant[] = {
      {left + 2.0f, top + 0.5f}, {left + side - 1.0f, top + 2.5f},
      {left + 2.0f, top + 5.5f}, {left + 2.0f, top + 0.5f},
    };
    strokePath(renderer, pennant, 4, color);
    fill(renderer, {left + 3.0f, top + 1.5f, 5.0f, 3.0f}, color);
    return true;
  }
  if(id == "bookmark") {
    const SDL_FPoint ribbon[] = {
      {left + 2.0f, top + side - 1.0f}, {left + 2.0f, top + 1.0f},
      {left + side - 2.0f, top + 1.0f}, {left + side - 2.0f, top + side - 1.0f},
      {cx, top + side - 4.0f},          {left + 2.0f, top + side - 1.0f},
    };
    strokePath(renderer, ribbon, 6, color);
    return true;
  }
  if(id == "tag") {
    // A luggage label: square at the string end, pointed at the other, with the
    // hole the string goes through near the square end. The hole is what says
    // "tag" rather than "arrow" -- without it the shape is a chevron.
    const SDL_FPoint label[] = {
      {left + 1.0f, top + 2.0f},        {left + side - 4.5f, top + 2.0f},
      {left + side - 1.0f, cy},         {left + side - 4.5f, top + side - 2.0f},
      {left + 1.0f, top + side - 2.0f}, {left + 1.0f, top + 2.0f},
    };
    strokePath(renderer, label, 6, color);
    fill(renderer, {left + 3.0f, cy - 1.0f, 2.0f, 2.0f}, color);
    return true;
  }
  if(id == "folder") {
    // The tab first, then the body under it: two rules meeting at the tab's
    // right shoulder is what reads as a folder at this size.
    const SDL_FPoint folder[] = {
      {left + 1.0f, top + side - 2.0f}, {left + 1.0f, top + 2.0f},
      {left + 5.0f, top + 2.0f},        {left + 6.5f, top + 4.0f},
      {left + side - 1.0f, top + 4.0f}, {left + side - 1.0f, top + side - 2.0f},
      {left + 1.0f, top + side - 2.0f},
    };
    strokePath(renderer, folder, 7, color);
    return true;
  }
  if(id == "calendar") {
    stroke(renderer, {left + 1.0f, top + 2.0f, side - 2.0f, side - 3.0f}, color);
    // The filled band is the month header; the two ticks above it are the
    // rings. Without them the mark is a picture frame.
    fill(renderer, {left + 1.0f, top + 2.0f, side - 2.0f, 3.0f}, color);
    SDL_RenderLine(renderer, left + 3.5f, top, left + 3.5f, top + 2.0f);
    SDL_RenderLine(renderer, left + side - 3.5f, top, left + side - 3.5f, top + 2.0f);
    return true;
  }
  if(id == "clock") {
    strokeCircle(renderer, cx, cy, side / 2.0f - 1.0f, color);
    // Hands at twelve and four, drawn as filled bars rather than as lines: a
    // one-pixel diagonal inside a one-pixel ring is lost against the ring, and
    // a clock face with no hands on it is a circle.
    fill(renderer, {std::round(cx), std::round(cy) - 3.0f, 1.0f, 4.0f}, color);
    fill(renderer, {std::round(cx), std::round(cy), 3.0f, 1.0f}, color);
    return true;
  }
  if(id == "bolt") {
    // Two strokes down and one across, doubled sideways for weight -- a
    // single-pixel lightning bolt reads as a scratch.
    for(float d = 0.0f; d <= 1.0f; d += 1.0f) {
      const SDL_FPoint bolt[] = {
        {left + 7.0f + d, top},        {left + 3.0f + d, cy + 0.5f},
        {left + 6.0f + d, cy + 0.5f},  {left + 4.0f + d, top + side},
      };
      strokePath(renderer, bolt, 4, color);
    }
    return true;
  }
  if(id == "warning") {
    // The apex is cut flat by a pixel. At twelve pixels the two sides meet
    // inside one row and the join draws a stray dot above the triangle, which
    // reads as a mark of its own rather than as a point.
    const float apex = std::round(cx);
    const SDL_FPoint triangle[] = {
      {apex, top + 1.0f},               {left + side - 0.5f, top + side - 1.0f},
      {left + 0.5f, top + side - 1.0f}, {apex, top + 1.0f},
    };
    strokePath(renderer, triangle, 4, color);
    // The bar and its dot sit in the lower two thirds, where the triangle is
    // wide enough for them to have air either side.
    fill(renderer, {apex, top + 5.0f, 1.0f, 3.0f}, color);
    fill(renderer, {apex, top + 9.0f, 1.0f, 1.0f}, color);
    return true;
  }
  if(id == "code") {
    // `< >`, the two chevrons the chrome already draws for a disclosure, turned
    // outward and set either side of the centre.
    SDL_RenderLine(renderer, left + 4.5f, top + 2.0f, left + 1.0f, cy);
    SDL_RenderLine(renderer, left + 1.0f, cy, left + 4.5f, top + side - 2.0f);
    SDL_RenderLine(renderer, left + side - 4.5f, top + 2.0f, left + side - 1.0f, cy);
    SDL_RenderLine(renderer, left + side - 1.0f, cy, left + side - 4.5f, top + side - 2.0f);
    return true;
  }
  return false;
}

void drawWindowGlyph(SDL_Renderer* renderer, Rect box, std::size_t which, bool maximized,
                     SDL_Color color) {
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  const auto line = [&](float x1, float y1, float x2, float y2) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(renderer, x1, y1, x2, y2);
  };
  if(which == 0) {
    hLine(renderer, cx - 4.0f, cx + 4.0f, cy, color);
    return;
  }
  if(which == 1) {
    if(maximized) {
      // Two offset outlines: the restored window in front of the space it
      // currently fills.
      stroke(renderer, {cx - 4.0f, cy - 1.0f, 7.0f, 7.0f}, color);
      hLine(renderer, cx - 1.0f, cx + 3.0f, cy - 4.0f, color);
      line(cx + 3.0f, cy - 4.0f, cx + 3.0f, cy + 1.0f);
    } else {
      stroke(renderer, {cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, color);
    }
    return;
  }
  line(cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f);
  line(cx - 4.0f, cy + 4.0f, cx + 4.0f, cy - 4.0f);
}

void drawTagDot(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  // A solid disc, drawn as spans rather than as a texture: at seven pixels a
  // circle is a handful of rows and the arithmetic is cheaper than a cache
  // lookup.
  //
  // Solid, and not an outline for one state and a fill for another. That was
  // tried, to mark the tag being filtered by, and at this size the ring's hole
  // was over half the dot -- so the mark read as a small "0" rather than as a
  // dot, in every row, to distinguish a state the row's own accent strip
  // already says. Two pixels is not enough room for two states.
  const float radius = std::min(box.w, box.h) / 2.0f;
  const float cx = box.x + box.w / 2.0f;
  const float cy = box.y + box.h / 2.0f;
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  for(float dy = -radius; dy <= radius; dy += 1.0f) {
    const float half = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
    if(half <= 0.0f) continue;
    SDL_RenderLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
  }
}

}
