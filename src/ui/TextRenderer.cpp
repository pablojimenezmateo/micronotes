#include "ui/TextRenderer.h"

#include "ui/TextFit.h"

namespace micronotes::ui {

std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, const TextStyle& style) {
  perf::addCounter(perf::CounterId::ShellEllipsizeCalls);
  return ellipsizeToFit(std::move(value), maxWidth, [&](std::string_view candidate) {
    perf::addCounter(perf::CounterId::ShellEllipsizeMeasures);
    return text.width(candidate, style);
  });
}

std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading, bool mono) {
  return ellipsizeToWidth(text, std::move(value), maxWidth, TextRenderer::styleFor(heading, mono, false, false));
}

}
