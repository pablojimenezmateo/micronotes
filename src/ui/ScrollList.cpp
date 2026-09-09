#include "ui/ScrollList.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {

int WheelAccumulator::take(float notches, float unitsPerNotch) {
  remainder += -notches * unitsPerNotch;
  const float whole = std::trunc(remainder);
  remainder -= whole;
  return static_cast<int>(whole);
}

int RowStrip::last(std::size_t count) const {
  return std::max(0, static_cast<int>(count) - std::max(1, shown));
}

void RowStrip::clamp(std::size_t count) {
  scroll = std::clamp(scroll, 0, last(count));
}

void RowStrip::scrollBy(int rows, std::size_t count) {
  scroll = std::clamp(scroll + rows, 0, last(count));
}

void RowStrip::reveal(int index) {
  // Both ends, and in this order: a list that has just shrunk can leave the
  // offset past the row, and clamping to the near edge first would then send it
  // straight back out the other side.
  if(index < scroll) scroll = index;
  else if(index >= scroll + std::max(1, shown)) scroll = index - std::max(1, shown) + 1;
  scroll = std::max(0, scroll);
}

void RowStrip::fitted(std::size_t rows) {
  shown = std::max(1, static_cast<int>(rows));
}

void ScrollList::setContent(float viewport, float content) {
  maxScroll_ = std::max(0, static_cast<int>(std::ceil(content - std::max(1.0f, viewport))));
  scrollTo(scroll_);
}

void ScrollList::scrollTo(int value) {
  scroll_ = std::clamp(value, 0, maxScroll_);
}

void ScrollList::scrollBy(int delta) {
  scrollTo(scroll_ + delta);
}

void ScrollList::wheel(float notches, float unitsPerNotch) {
  scrollBy(wheel_.take(notches, unitsPerNotch));
}

void ScrollList::reveal(float top, float bottom, float height) {
  const auto offset = static_cast<float>(scroll_);
  if(top < offset) scrollTo(static_cast<int>(std::floor(top)));
  else if(bottom > offset + height) scrollTo(static_cast<int>(std::ceil(bottom - height)));
  else scrollTo(scroll_);
}

void ScrollList::rebase() {
  scroll_ = 0;
  wheel_.remainder = 0.0f;
}

}
