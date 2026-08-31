#pragma once

#include <string_view>

namespace micronotes::ui {

// Appearance preferences that are not colours. They live beside the theme in
// `.micronotes/ui.state`, because neither is a property of any note: the same
// library read on a laptop and on a monitor wants different sizes, and the
// files must not change because someone made the text bigger.
//
// Named steps rather than free numbers. Three steps are one keystroke to pick
// and impossible to get wrong; a pixel field is a decision, and a hand-edited
// state file could put the app somewhere it cannot be read from.

enum class TextSize {
  Small,
  Medium,
  Large
};

enum class PageWidth {
  Narrow,
  Medium,
  Wide
};

TextSize textSize();
void setTextSize(TextSize size);
// Multiplier applied to every size in the type scale.
float textScale();

PageWidth pageWidth();
void setPageWidth(PageWidth width);
// The widest the content column may grow, in logical pixels. Extra room
// becomes margin rather than more characters per line.
float pageWidthPx();

// Round-trip through the persisted UI state file.
std::string_view textSizeName(TextSize size);
TextSize textSizeFromName(std::string_view name);
std::string_view pageWidthName(PageWidth width);
PageWidth pageWidthFromName(std::string_view name);

// What the settings list shows beside each row.
std::string_view textSizeLabel(TextSize size);
std::string_view pageWidthLabel(PageWidth width);

}
