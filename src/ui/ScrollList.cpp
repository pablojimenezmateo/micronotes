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
