#include "ui/DocRuns.h"

#include "ui/DocStyle.h"
#include "ui/Painter.h"
#include "ui/Theme.h"

namespace micronotes::ui {

std::size_t paintRuns(SDL_Renderer* renderer, TextRenderer& text, const doc::BlockLayout& layout,
                      const RunPaint& paint) {
  const Theme& palette = theme();
  const bool banded = paint.to > paint.from;
  std::size_t drawn = 0;
  for(const auto& line : layout.lines) {
    if(banded && (line.y + line.height <= paint.from || line.y >= paint.to)) continue;
    const float lineY = paint.y + line.y;
    // Every tinted ground on this line, before any of its text. See
    // `forEachCodeSpan` for why the order is the whole point.
    forEachCodeSpan(layout.runsOf(line), [&](float spanX, float spanWidth) {
      fill(renderer, {paint.x + spanX - 2.0f, lineY + 1.0f, spanWidth + 4.0f, line.height - 2.0f},
           palette.codeBackground);
    });
    for(const auto& run : layout.runsOf(line)) {
      if(run.text.empty()) continue;
      ++drawn;
      const float x = paint.x + run.rect.x;
      const SDL_Color ink =
        run.role == doc::TextRole::Body ? paint.bodyInk : inkFor(palette, run.role);
      text.draw(run.text, x, lineY, ink, textStyleFor(run.style));

      if(run.style.strike) {
        hLine(renderer, x, x + run.rect.w, lineY + line.height * 0.45f, ink);
      }
      if(run.linkIndex < 0 || run.linkIndex >= static_cast<int>(layout.links.size())) continue;
      // No rule under an image's caption: the picture below it is the
      // affordance, and an underline there reads as a stray link. The rect is
      // still handed back -- the alt text is how a picture is clicked.
      //
      // The rule runs through the spaces inside a multi-word link rather than
      // breaking up at them: the space between two words of a link is a run of
      // the link, so it is underlined with them.
      if(run.role != doc::TextRole::ImageAlt) {
        hLine(renderer, x, x + run.rect.w, lineY + line.height - 4.0f, palette.accent);
      }
      if(paint.links == nullptr) continue;
      const bool wiki = run.role == doc::TextRole::WikiLink ||
                        run.role == doc::TextRole::WikiLinkUnresolved;
      paint.links->push_back({{x, lineY, run.rect.w, line.height},
                              layout.links[static_cast<std::size_t>(run.linkIndex)], wiki});
    }
  }
  return drawn;
}

}
