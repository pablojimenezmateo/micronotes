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
// The grid every mark in this file is laid out on: one pixel at the centre,
// and whole-pixel offsets either side of it.
//
// The marks used to be placed in a twelve-pixel field, and twelve pixels have
// no middle one -- the centre falls on the seam between the sixth and the
// seventh. A pair of coordinates written to mirror each other across that seam,
// `left + 4.5` and `left + side - 4.5`, are both halves, and the renderer
// rounds two halves the same way rather than opposite ways. So the arm on the
// right landed a pixel wide of its partner on the left, and that is the whole
// of what was wrong with the diagonals: the two halves of `< >` stepped
// differently, the warning triangle's right edge sat a pixel out from its left,
// the tag's point and the clock's ring leaned, and the star -- mirror-perfect
// as a polygon -- lost the right edge of its widest row.
//
// Eleven pixels have a middle one. Every offset below is a whole number of
// pixels from it, so `cx - d` and `cx + d` name pixels that are exact mirrors
// and no coordinate lands on a half where the renderer has to choose. An odd
// field cannot sit exactly centred in an even box, so a mark is half a pixel
// off the middle of its button -- which is invisible, where a mark that is not
// symmetric about itself is not.
constexpr float kGlyphRadius = 5.0f;

struct Grid {
  float cx = 0.0f;
  float cy = 0.0f;
};

// Floor rather than round, so a mark that hands its own field on to another --
// `drawNoteGlyph` gives its field to `drawStarGlyph` -- gets back the centre it
// passed in rather than one a pixel along.
Grid gridFor(Rect box) {
  return Grid {std::floor(box.x + box.w / 2.0f), std::floor(box.y + box.h / 2.0f)};
}

// The square field that grid describes, for the marks that hand it on.
Rect fieldOf(Grid grid) {
  const float side = kGlyphRadius * 2.0f + 1.0f;
  return Rect {grid.cx - kGlyphRadius, grid.cy - kGlyphRadius, side, side};
}

// A ring, plotted with the eight-way symmetry of a midpoint circle so that the
// four quadrants are exact reflections of one another.
//
// This was a twenty-four-sided polygon, which is symmetric on paper and was not
// on the grid: SDL rounds each edge's two endpoints on its own, so the clock
// face came out six pixels wide across the top, a single stray pixel across the
// bottom, and a row taller on one side than the other. A midpoint circle places
// one pixel and reflects it into the other seven octants, so there is nothing
// left for rounding to do differently on one side than another.
void strokeCircle(SDL_Renderer* renderer, float cx, float cy, float r, SDL_Color color) {
  const int radius = static_cast<int>(std::lround(r));
  if(radius <= 0) return;
  // Eight points per step, and at most a step per row of the radius. The
  // ceiling stands in for a ring far larger than anything the shell draws.
  constexpr int kMaxRingPoints = 8 * 64;
  SDL_FPoint ring[kMaxRingPoints];
  int count = 0;
  const auto plot = [&](int dx, int dy) {
    ring[count++] = SDL_FPoint {cx + static_cast<float>(dx), cy + static_cast<float>(dy)};
  };
  int x = radius;
  int y = 0;
  int err = 1 - radius;
  while(x >= y && count + 8 <= kMaxRingPoints) {
    plot(x, y);
    plot(-x, y);
    plot(x, -y);
    plot(-x, -y);
    plot(y, x);
    plot(-y, x);
    plot(y, -x);
    plot(-y, -x);
    ++y;
    if(err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderPoints(renderer, ring, count);
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
  const auto [cx, cy] = gridFor(box);
  // Two strokes, the short arm down-right and the long one up-right. Doubled a
  // pixel apart so the tick has some weight against a row's ground; a
  // single-pixel tick disappears next to the label beside it.
  //
  // Both arms at forty-five degrees. The long one used to run five across
  // against six up, which is not a slope the grid can step evenly: it came out
  // as a stair with one tread twice the depth of the others, right where the
  // eye follows the tick up. At forty-five degrees each arm steps once per row.
  //
  // The pair also sat three rows below the middle of its box and hung out of
  // the bottom of it -- every checkbox in the shell wore a tick a pixel low.
  // Equal arms are what let it centre: seven rows about `cy`, four of them
  // either side of the doubling.
  for(float d = 0.0f; d <= 1.0f; d += 1.0f) {
    SDL_RenderLine(renderer, cx - 4.0f, cy - 1.0f + d, cx - 1.0f, cy + 2.0f + d);
    SDL_RenderLine(renderer, cx - 1.0f, cy + 2.0f + d, cx + 4.0f, cy - 3.0f + d);
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

void drawArrowGlyph(SDL_Renderer* renderer, Rect box, ArrowDirection direction, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const auto [cx, cy] = gridFor(box);
  const bool vertical = direction == ArrowDirection::Up || direction == ArrowDirection::Down;
  // The head is a chevron half as deep as it is wide -- the proportion the
  // disclosure chevron and the close cross are drawn at too -- which is the
  // same thing as saying its two arms are at forty-five degrees. So there is
  // only one number to choose here, and the other follows from it.
  //
  // It used to choose both, and independently: an arm from a fraction of the
  // button, then a tip from half of that, each rounded on its own. Whenever the
  // arm came out odd the two stopped agreeing, and every box the shell actually
  // hands this is one of those. The tab strip's chevrons are handed 16x34, so
  // the arm was 7 against a tip of 4 -- a slope of eight in seven, which the
  // grid cannot step evenly. It came out as a run of single steps with one
  // tread doubled, and the doubled tread fell in a different place on each arm:
  // the upper one stepped 15, 14, 13, 12, then 11 and 10 together, while the
  // lower one stepped 10, 11, 12, then 13 and 14 together. Hence a chevron that
  // was neither a straight diagonal nor the same shape above the tip as below
  // it. The find bar's 24x24 and the settings steppers' 20x22 were both wrong
  // the same way.
  //
  // At forty-five degrees every step is a diagonal one, so there is no tie for
  // the renderer to break and nothing left to come out differently on the two
  // arms.
  //
  // `across` is the axis the head spans and `along` the one it points down, so
  // a wide short button and a tall narrow one both get a head that fits: the
  // depth is capped at half the room it has to point into.
  const float across = vertical ? box.w : box.h;
  const float along = vertical ? box.h : box.w;
  const float tip = std::max(2.0f, std::min(std::round(across * 0.125f), std::floor(along * 0.5f)));
  const float arm = tip * 2.0f;
  // Both arms drawn from the tip outward rather than as a polyline through it.
  // Nothing depends on that while the arms are at forty-five degrees, and it is
  // what keeps them a reflected pair if they ever are not.
  if(vertical) {
    const float dy = direction == ArrowDirection::Down ? tip : -tip;
    SDL_RenderLine(renderer, cx, cy + dy, cx - arm, cy - dy);
    SDL_RenderLine(renderer, cx, cy + dy, cx + arm, cy - dy);
    return;
  }
  const float dx = direction == ArrowDirection::Right ? tip : -tip;
  SDL_RenderLine(renderer, cx + dx, cy, cx - dx, cy - arm);
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
  const float head = std::max(2.0f, std::round(box.h * 0.22f));
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

namespace {

// A mark drawn from one closed hull, filled or as an outline.
//
// Two marks are drawn this way -- the star a note can wear and the pin that
// says a note is kept to hand -- and both need the same two things from the
// rasteriser, neither of which it gives for free: a filled state and an
// outline state that are the *same* silhouette, and left-right symmetry to the
// pixel. Written twice, the second copy would have had to rediscover
// everything below.
//
// The hull is closed (`hull[count] == hull[0]`), has an even `count`, and is
// laid out so that vertex `i` is the mirror of vertex `count - i`. That
// pairing is a property of how each hull is written rather than something this
// can check, and the outline depends on it.
//
// `cx`, `cy` are the centre *pixel*; the hull's own coordinates are about the
// middle of that pixel, half a pixel along. That half pixel is the whole
// reason these marks are symmetric. A shape centred on the coordinate is
// centred on the seam at the pixel's leading edge, so its two halves fall
// either side of that seam and reflect onto each other a pixel out of step;
// centred on the pixel's middle, a column and its reflection are equally far
// from the middle of the same pixel, and every rule below reflects exactly.
void drawHull(SDL_Renderer* renderer, const SDL_FPoint* hull, int count, float cx, float cy,
              bool filled, SDL_Color color) {
  if(count < 4) return;
  const float gx = cx + 0.5f;
  const float gy = cy + 0.5f;

  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  if(!filled) {
    // The hull is symmetric to the last decimal place and was not rasterised
    // that way, for two reasons, and both are about direction.
    //
    // Its vertices sit between pixels, and the renderer rounds each end of each
    // edge on its own, so a pair of edges that mirrored each other exactly
    // rounded onto pixels that did not. So they are snapped here -- as an
    // offset from the centre pixel, which is what keeps the pair mirrored,
    // since `std::round` breaks its ties away from zero and so takes `+d` and
    // `-d` opposite ways even when `d` lands on a half.
    //
    // Then the edges go down in mirrored pairs rather than as one walk around
    // the hull. A polyline is travelled in one direction, so the star's left
    // half was walked against its right -- and a line rasteriser breaks the tie
    // on a slope passing exactly between two pixels by the direction it is
    // travelling, not by the geometry. Reflection maps vertex `i` to vertex
    // `count - i`, so drawing each edge together with that reflection, and
    // counting down so the reflection is travelled the reflected way, has the
    // pair come out as mirror images.
    constexpr int kMaxHullPoints = 32;
    if(count >= kMaxHullPoints) return;
    SDL_FPoint outline[kMaxHullPoints];
    for(int i = 0; i <= count; ++i) {
      outline[i] = SDL_FPoint {cx + std::round(hull[i].x - gx), cy + std::round(hull[i].y - gy)};
    }
    for(int i = 0; i < count / 2; ++i) {
      const int m = (count - i) % count;
      const int n = (count - i - 1) % count;
      SDL_RenderLine(renderer, outline[i].x, outline[i].y, outline[i + 1].x, outline[i + 1].y);
      SDL_RenderLine(renderer, outline[m].x, outline[m].y, outline[n].x, outline[n].y);
    }
    return;
  }
  // Scanline fill, even-odd: for each row, every crossing of the outline in
  // order, filled between the first pair, the second pair, and so on.
  //
  // It used to take only the leftmost and rightmost crossing, which is a
  // convex-hull fill -- and a star is the textbook non-convex polygon, so the
  // two lower points were swallowed and what the mark actually drew was a
  // lump. Sorting the crossings is the whole difference, and neither hull here
  // has a row with more than six.
  //
  // Three spans a row over at most the mark's own height: a named ceiling
  // rather than the same `192` written at three of the four places that had to
  // agree about it.
  constexpr int kMaxSpans = 192;
  constexpr int kMaxCrossings = 32;
  if(count > kMaxCrossings) return;
  float lowest = hull[0].y;
  float highest = hull[0].y;
  for(int i = 1; i < count; ++i) {
    lowest = std::min(lowest, hull[i].y);
    highest = std::max(highest, hull[i].y);
  }
  SDL_FRect spans[kMaxSpans];
  int spanCount = 0;
  const int top = static_cast<int>(std::floor(lowest));
  const int bottom = static_cast<int>(std::ceil(highest));
  for(int y = top; y <= bottom && spanCount + 3 < kMaxSpans; ++y) {
    const float row = static_cast<float>(y) + 0.5f;
    float crossings[kMaxCrossings];
    int found = 0;
    for(int i = 0; i < count; ++i) {
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
    for(int i = 0; i + 1 < found && spanCount < kMaxSpans; i += 2) {
      // The columns whose own middle the span covers, as a pair of columns.
      //
      // A left edge plus a rounded *width* was the fault here: the two roundings
      // do not cancel, and on the star's widest row -- nine and a half pixels
      // across -- the one they dropped was always the rightmost. So the mark
      // leaned a pixel left at every size, with one arm two pixels longer than
      // the other. `std::ceil` and `std::floor` about a shape centred on a
      // pixel's middle are exact reflections of each other, where rounding a
      // width is a reflection of nothing.
      const float left = std::ceil(crossings[i] - 0.5f);
      const float right = std::floor(crossings[i + 1] - 0.5f);
      if(right < left) {
        // Narrower than a pixel: the nearest single column rather than nothing.
        // The star's two lower legs are thinner than a pixel for most of their
        // length at this size, and the pin's needle is thinner than one for the
        // whole of its; dropped, the star reads as a triangle with a spike on
        // top and the pin as a head with nothing under it. Rounding the span's
        // own leading edge keeps the pixel it picks a reflection of the pixel
        // its mirror picks.
        const float only = std::round(crossings[i] - 0.5f);
        spans[spanCount++] = SDL_FRect {only, static_cast<float>(y), 1.0f, 1.0f};
        continue;
      }
      spans[spanCount++] =
        SDL_FRect {left, static_cast<float>(y), right - left + 1.0f, 1.0f};
    }
  }
  if(spanCount > 0) SDL_RenderFillRects(renderer, spans, spanCount);
}

// The largest whole radius that fits in `box` with the centre pixel counted in.
//
// It used to be half the box less one, which on an even box is a half -- a
// radius measured in half pixels from a centre measured in whole ones, so
// nothing about the mark landed where it was asked to.
float hullRadius(Rect box) {
  return std::max(4.0f, std::floor((std::min(box.w, box.h) - 1.0f) / 2.0f));
}

}

void drawStarGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  const float r = hullRadius(box);
  // Five points, alternating outer and inner radius, in exact coordinates. Both
  // states are drawn from this one hull, so a filled star and an outline are
  // the same star -- an outline of a different one would read as a second mark
  // rather than as the same mark un-set.
  //
  // The same hull, not the same pixels: without anti-aliasing the two states
  // have to disagree about the five tips by a pixel. A tip reaching 4.755
  // pixels out covers a fifth of the column at 5, so the fill stops at 4 -- the
  // last column whose middle it actually covers -- while the outline puts the
  // tip on the nearest column, which is 5. Rounding the outline in instead
  // would put it three quarters of a pixel short of the silhouette rather than
  // a quarter past it.
  constexpr int kPoints = 10;
  // A waist a little over half the outer radius. It was 0.42, near the ratio a
  // pentagram gives, which is right where a point has room to taper and is
  // wrong here: at eleven pixels it left the top point three rows tall and one
  // pixel wide -- a spike rather than a point -- and closed the notch between
  // the two lower legs. Widening the waist shortens the points, which is what
  // buys them width at a size where a point one pixel across is not a point.
  constexpr float kWaist = 0.56f;
  const float gx = cx + 0.5f;
  const float gy = cy + 0.5f;
  SDL_FPoint hull[kPoints + 1];
  for(int i = 0; i < kPoints; ++i) {
    const float radius = (i % 2 == 0) ? r : r * kWaist;
    // Starting at -90 degrees, so a point sits at the top where the eye looks.
    const float angle = -1.5707963f + static_cast<float>(i) * 3.14159265f / 5.0f;
    hull[i] = SDL_FPoint {gx + std::cos(angle) * radius, gy + std::sin(angle) * radius};
  }
  hull[kPoints] = hull[0];
  drawHull(renderer, hull, kPoints, cx, cy, filled, color);
}

void drawPinGlyph(SDL_Renderer* renderer, Rect box, bool filled, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  const float r = hullRadius(box);
  const float gx = cx + 0.5f;
  const float gy = cy + 0.5f;

  // A thumbtack seen side-on: a head, the wider flange your thumb pushes
  // against, and a needle tapering to a point. The flange is what makes it a
  // tack rather than a nail, and at this size it is the only part with room to
  // say so -- a head and a needle alone read as a lollipop.
  //
  // Fractions of the field's radius rather than pixel counts, so the mark keeps
  // its proportions at whatever size a caller hands it. Not rounded: an edge
  // wants to fall *between* two pixels, and at the eleven pixels the breadcrumb
  // and the tab strip use these land on halves exactly -- a head five columns
  // across over a flange nine across. Rounding them was the first draft and it
  // widened the head to seven and deepened the flange to four rows, which is an
  // anvil rather than a tack.
  const float headHalf = r * 0.5f;        // half the head's width
  const float flangeHalf = r * 0.9f;      // half the flange's width
  // The two ends sit at the radius exactly, not half a pixel past it, for the
  // reason the star's tips do: the outline snaps each vertex to the nearest
  // pixel, and a half rounds *away* from the centre -- so a top edge written at
  // `-r - 0.5` put the outline's head a row outside the field and the bar
  // across the top of the mark simply did not draw.
  const float headTop = -r;
  const float flangeTop = -r * 0.5f;
  const float flangeFoot = -r * 0.1f;
  const float needleHalf = 0.5f;
  const float tip = r;

  // Clockwise from the middle of the flat top, and written so that vertex `i`
  // is the mirror of vertex `count - i` -- which is what `drawHull`'s outline
  // pass requires, and which nothing but the order below establishes. The two
  // vertices on the axis, the top and the needle's tip, are their own mirrors.
  constexpr int kPoints = 12;
  const SDL_FPoint hull[kPoints + 1] = {
    {gx,                gy + headTop},      // 0  top centre
    {gx + headHalf,     gy + headTop},      // 1  top right of the head
    {gx + headHalf,     gy + flangeTop},    // 2  where the head meets the flange
    {gx + flangeHalf,   gy + flangeTop},    // 3  the flange's right shoulder
    {gx + flangeHalf,   gy + flangeFoot},   // 4  its right foot
    {gx + needleHalf,   gy + flangeFoot},   // 5  where the needle leaves it
    {gx,                gy + tip},          // 6  the point
    {gx - needleHalf,   gy + flangeFoot},   // 7
    {gx - flangeHalf,   gy + flangeFoot},   // 8
    {gx - flangeHalf,   gy + flangeTop},    // 9
    {gx - headHalf,     gy + flangeTop},    // 10
    {gx - headHalf,     gy + headTop},      // 11
    {gx,                gy + headTop},      // 12 == 0
  };
  drawHull(renderer, hull, kPoints, cx, cy, filled, color);
}

void drawFileGlyph(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  // The same eleven-pixel field the note marks use, so a file beside a note
  // sits on the same baseline.
  const auto [cx, cy] = gridFor(box);
  // The sheet, with the top-right corner turned over, and the fold drawn as the
  // two edges of the triangle that was turned.
  //
  // The corner cut used to run three across against three down from
  // coordinates that were a pixel apart in one axis and three in the other, so
  // the diagonal rasterised as a step, a stray pixel and a second step, and the
  // fold's two strokes crossed it rather than meeting it -- at twelve pixels
  // the corner read as a smudge instead of a fold. Three across against three
  // down from whole pixels is one clean diagonal, and the fold's corner lands
  // on it exactly.
  const SDL_FPoint sheet[] = {
    {cx - 4.0f, cy - 5.0f}, {cx + 1.0f, cy - 5.0f}, {cx + 4.0f, cy - 2.0f},
    {cx + 4.0f, cy + 5.0f}, {cx - 4.0f, cy + 5.0f}, {cx - 4.0f, cy - 5.0f},
  };
  strokePath(renderer, sheet, 6, color);
  const SDL_FPoint fold[] = {
    {cx + 1.0f, cy - 5.0f},
    {cx + 1.0f, cy - 2.0f},
    {cx + 4.0f, cy - 2.0f},
  };
  strokePath(renderer, fold, 3, color);
}

namespace {

// The eleven note marks, one function each.
//
// Each takes the button it is drawn in and works in the eleven-pixel field
// `gridFor` centres inside it, so one set of offsets serves a 16px sidebar row
// and a 30px picker cell alike, and so that a mark meant to be symmetric is
// symmetric. See `kGlyphRadius`.
//
// Named functions rather than the arms of one `if(id == ...)` chain, because
// the chain was the *second* list of these marks: `kNoteGlyphs` below is the
// first, and the picker is built from that one. Two lists of the same eleven
// things with nothing holding them level -- a mark added to the table and not
// the chain is a picker cell that draws nothing, and one added to the chain
// and not the table cannot be chosen at all, and neither fails to compile.
// The table carries the function now, so there is one list.

void drawStarNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  drawStarGlyph(renderer, fieldOf(gridFor(box)), true, color);
}

void drawCheckNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  drawCheckGlyph(renderer, fieldOf(gridFor(box)), color);
}

void drawFlagNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // A pole with the pennant on its top half, so the mark has a baseline the
  // way a letter does.
  SDL_RenderLine(renderer, cx - 4.0f, cy - 5.0f, cx - 4.0f, cy + 5.0f);
  // The pennant filled row by row rather than outlined and then filled
  // separately. Its two edges are one slope mirrored about the row the point
  // sits on, so measuring each row's reach from that row gives the rows in
  // mirrored pairs -- where an outline plus a fill inside it was two
  // different shapes rounding two different ways, and the pennant came out
  // with a step on its lower edge that its upper edge did not have.
  for(int dy = -5; dy <= 1; ++dy) {
    const float fromPoint = std::abs(static_cast<float>(dy) + 2.0f);
    const float reach = std::round(7.0f * (3.0f - fromPoint) / 3.0f);
    hLine(renderer, cx - 3.0f, cx - 3.0f + reach, cy + static_cast<float>(dy), color);
  }
}

void drawBookmarkNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // The notch cut at forty-five degrees from each bottom corner, meeting on
  // the centre column.
  const SDL_FPoint ribbon[] = {
    {cx - 4.0f, cy + 5.0f}, {cx - 4.0f, cy - 5.0f}, {cx + 4.0f, cy - 5.0f},
    {cx + 4.0f, cy + 5.0f}, {cx, cy + 1.0f},        {cx - 4.0f, cy + 5.0f},
  };
  strokePath(renderer, ribbon, 6, color);
}

void drawTagNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // A luggage label: square at the string end, pointed at the other, with the
  // hole the string goes through near the square end. The hole is what says
  // "tag" rather than "arrow" -- without it the shape is a chevron.
  //
  // The point is two forty-five degree edges meeting on the centre row, so
  // the upper and lower halves step identically. They used to start from a
  // half coordinate, which the renderer took the same way for both, and the
  // point came out a row off the middle of the label it belongs to.
  const SDL_FPoint label[] = {
    {cx - 5.0f, cy - 4.0f}, {cx + 1.0f, cy - 4.0f}, {cx + 5.0f, cy},
    {cx + 1.0f, cy + 4.0f}, {cx - 5.0f, cy + 4.0f}, {cx - 5.0f, cy - 4.0f},
  };
  strokePath(renderer, label, 6, color);
  // Three pixels square, not two: an even hole cannot sit centred on an odd
  // field, and a hole one row off centre in a nine-row label is visible.
  fill(renderer, {cx - 3.0f, cy - 1.0f, 3.0f, 3.0f}, color);
}

void drawFolderNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // The tab first, then the body under it: two rules meeting at the tab's
  // right shoulder is what reads as a folder at this size. The shoulder is
  // two across against two down -- the one diagonal here that is meant to be
  // short, and the only mark in the set that is meant to be lopsided.
  const SDL_FPoint folder[] = {
    {cx - 5.0f, cy + 4.0f}, {cx - 5.0f, cy - 4.0f}, {cx - 1.0f, cy - 4.0f},
    {cx + 1.0f, cy - 2.0f}, {cx + 5.0f, cy - 2.0f}, {cx + 5.0f, cy + 4.0f},
    {cx - 5.0f, cy + 4.0f},
  };
  strokePath(renderer, folder, 7, color);
}

void drawCalendarNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  stroke(renderer, {cx - 5.0f, cy - 3.0f, 11.0f, 9.0f}, color);
  // The filled band is the month header; the two ticks above it are the
  // rings. Without them the mark is a picture frame.
  fill(renderer, {cx - 5.0f, cy - 3.0f, 11.0f, 3.0f}, color);
  SDL_RenderLine(renderer, cx - 3.0f, cy - 5.0f, cx - 3.0f, cy - 3.0f);
  SDL_RenderLine(renderer, cx + 3.0f, cy - 5.0f, cx + 3.0f, cy - 3.0f);
}

void drawClockNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  strokeCircle(renderer, cx, cy, kGlyphRadius, color);
  // Hands at twelve and three, drawn as filled bars rather than as lines: a
  // one-pixel diagonal inside a one-pixel ring is lost against the ring, and
  // a clock face with no hands on it is a circle.
  fill(renderer, {cx, cy - 3.0f, 1.0f, 4.0f}, color);
  fill(renderer, {cx, cy, 3.0f, 1.0f}, color);
}

void drawBoltNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // Two strokes down and one across, doubled sideways for weight -- a
  // single-pixel lightning bolt reads as a scratch. Asymmetric on purpose,
  // and the one mark here where that is the shape rather than a fault.
  for(float d = 0.0f; d <= 1.0f; d += 1.0f) {
    const SDL_FPoint bolt[] = {
      {cx + 1.0f + d, cy - 5.0f},
      {cx - 3.0f + d, cy},
      {cx + d, cy},
      {cx - 2.0f + d, cy + 5.0f},
    };
    strokePath(renderer, bolt, 4, color);
  }
}

void drawWarningNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // Sides at one across for every two down. That is a slope the grid steps
  // exactly, and both sides pass through the apex pixel, so the point comes
  // out a point -- where the shallower join this had before drew a stray dot
  // a row above the triangle, which had to be cut flat by a pixel to hide it.
  // Cutting the apex is no longer needed.
  //
  // Three separate strokes rather than one closed path, and both sides drawn
  // *from the apex* outward. A one-in-two slope has a tie on every other row
  // -- the exact centre of the pixel pair it has to choose between -- and a
  // line rasteriser breaks that tie by the direction it is travelling, not by
  // the geometry. Walked as a loop, the right side ran apex-to-base and the
  // left base-to-apex, so the two sides broke their ties opposite ways and
  // the right edge stepped a row before its partner all the way down. From a
  // common origin with the run mirrored, they break them the same way.
  SDL_RenderLine(renderer, cx, cy - 5.0f, cx + 5.0f, cy + 5.0f);
  SDL_RenderLine(renderer, cx, cy - 5.0f, cx - 5.0f, cy + 5.0f);
  hLine(renderer, cx - 5.0f, cx + 5.0f, cy + 5.0f, color);
  // The bar and its dot on the centre column, in the lower two thirds where
  // the triangle is wide enough to leave air either side of them.
  fill(renderer, {cx, cy - 1.0f, 1.0f, 3.0f}, color);
  fill(renderer, {cx, cy + 3.0f, 1.0f, 1.0f}, color);
}

void drawCodeNote(SDL_Renderer* renderer, Rect box, SDL_Color color) {
  const auto [cx, cy] = gridFor(box);
  // `< >`, the two chevrons the chrome already draws for a disclosure, turned
  // outward and set either side of the centre. Three across against three
  // down, so each of the four arms steps once per row and the left pair and
  // the right pair are the same shape reflected.
  SDL_RenderLine(renderer, cx - 2.0f, cy - 3.0f, cx - 5.0f, cy);
  SDL_RenderLine(renderer, cx - 5.0f, cy, cx - 2.0f, cy + 3.0f);
  SDL_RenderLine(renderer, cx + 2.0f, cy - 3.0f, cx + 5.0f, cy);
  SDL_RenderLine(renderer, cx + 5.0f, cy, cx + 2.0f, cy + 3.0f);
}

// The whole set, in picker order, each with what draws it.
constexpr NoteGlyph kNoteGlyphs[] = {
  {"star", "Star", drawStarNote},
  {"check", "Done", drawCheckNote},
  {"flag", "Follow up", drawFlagNote},
  {"bookmark", "Bookmark", drawBookmarkNote},
  {"tag", "Label", drawTagNote},
  {"folder", "Collection", drawFolderNote},
  {"calendar", "Dated", drawCalendarNote},
  {"clock", "Waiting", drawClockNote},
  {"bolt", "Urgent", drawBoltNote},
  {"warning", "Careful", drawWarningNote},
  {"code", "Technical", drawCodeNote},
};

}

std::span<const NoteGlyph> noteGlyphs() {
  return std::span<const NoteGlyph>(kNoteGlyphs, std::size(kNoteGlyphs));
}

bool drawNoteGlyph(SDL_Renderer* renderer, std::string_view id, Rect box, SDL_Color color) {
  if(id.empty()) return false;
  for(const NoteGlyph& glyph : kNoteGlyphs) {
    if(glyph.id != id) continue;
    // Set once, here: several of the marks reach straight for `SDL_RenderLine`,
    // which takes the renderer's standing colour rather than one of its own.
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    glyph.draw(renderer, box, color);
    return true;
  }
  return false;
}

void drawWindowGlyph(SDL_Renderer* renderer, Rect box, std::size_t which, bool maximized,
                     SDL_Color color) {
  // Inset from the button's own edges rather than stepped out from its centre.
  // The three glyphs then scale with the button -- which is sized off the menu
  // bar's height -- instead of staying a fixed eight pixels in the middle of
  // it, and the restore glyph below can be two whole rectangles at any size.
  const float left = box.x + 4.0f;
  const float right = box.x + box.w - 4.0f;
  const float top = box.y + 4.0f;
  const float bottom = box.y + box.h - 4.0f;
  const float cy = box.y + box.h / 2.0f;
  const auto line = [&](float x1, float y1, float x2, float y2) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(renderer, x1, y1, x2, y2);
  };
  if(which == 0) {
    // A shade below centre, where a minimize bar is drawn everywhere else: on
    // the centre line it reads as a strikethrough of the empty button.
    hLine(renderer, left, right, std::round(cy + 2.0f), color);
    return;
  }
  if(which == 1) {
    if(maximized) {
      // Two whole offset outlines: the window it would restore to, in front of
      // the screen it currently fills. This used to be one outline plus two
      // loose strokes standing in for the second -- a rectangle missing its
      // left and bottom edges -- so at the size the bar actually draws it the
      // glyph read as a broken box rather than as two stacked windows.
      stroke(renderer, {left + 1.5f, top + 3.0f, box.w - 9.0f, box.h - 9.0f}, color);
      stroke(renderer, {left - 1.0f, top + 1.0f, box.w - 9.0f, box.h - 9.0f}, color);
    } else {
      stroke(renderer, {left, top + 1.0f, box.w - 8.0f, box.h - 8.0f}, color);
    }
    return;
  }
  line(left, top, right, bottom);
  line(right, top, left, bottom);
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
