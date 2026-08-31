#include "ui/Settings.h"

namespace micronotes::ui {
namespace {

TextSize activeTextSize = TextSize::Medium;
PageWidth activePageWidth = PageWidth::Medium;

}

TextSize textSize() {
  return activeTextSize;
}

void setTextSize(TextSize size) {
  activeTextSize = size;
}

float textScale() {
  switch(activeTextSize) {
    case TextSize::Small: return 0.88f;
    case TextSize::Large: return 1.15f;
    case TextSize::Medium: break;
  }
  return 1.0f;
}

PageWidth pageWidth() {
  return activePageWidth;
}

void setPageWidth(PageWidth width) {
  activePageWidth = width;
}

float pageWidthPx() {
  switch(activePageWidth) {
    case PageWidth::Narrow: return 580.0f;
    case PageWidth::Wide: return 900.0f;
    case PageWidth::Medium: break;
  }
  return 720.0f;
}

std::string_view textSizeName(TextSize size) {
  switch(size) {
    case TextSize::Small: return "small";
    case TextSize::Large: return "large";
    case TextSize::Medium: break;
  }
  return "medium";
}

TextSize textSizeFromName(std::string_view name) {
  if(name == "small") return TextSize::Small;
  if(name == "large") return TextSize::Large;
  return TextSize::Medium;
}

std::string_view pageWidthName(PageWidth width) {
  switch(width) {
    case PageWidth::Narrow: return "narrow";
    case PageWidth::Wide: return "wide";
    case PageWidth::Medium: break;
  }
  return "medium";
}

PageWidth pageWidthFromName(std::string_view name) {
  if(name == "narrow") return PageWidth::Narrow;
  if(name == "wide") return PageWidth::Wide;
  return PageWidth::Medium;
}

std::string_view textSizeLabel(TextSize size) {
  switch(size) {
    case TextSize::Small: return "Small";
    case TextSize::Large: return "Large";
    case TextSize::Medium: break;
  }
  return "Medium";
}

std::string_view pageWidthLabel(PageWidth width) {
  switch(width) {
    case PageWidth::Narrow: return "Narrow";
    case PageWidth::Wide: return "Wide";
    case PageWidth::Medium: break;
  }
  return "Medium";
}

}
