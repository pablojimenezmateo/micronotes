#include "export/PdfFaces.h"

#include "export/PdfPage.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace micronotes::exporting {
namespace {

using microcore::pdf::PdfDocument;
using microcore::pdf::SfntFont;

constexpr int index(bool mono, bool strong, bool italic) {
  return (mono ? 4 : 0) | (strong ? 2 : 0) | (italic ? 1 : 0);
}

// The name the PDF calls a face by. It is not used to *find* anything -- the
// font program is embedded -- but a reader shows it in its properties panel
// and a font with no name there reads as a broken file.
std::string baseNameFor(bool mono, bool strong, bool italic) {
  std::string name = mono ? "JetBrainsMono" : "Inter";
  if(strong && italic) return name + "-SemiBoldItalic";
  if(strong) return name + "-SemiBold";
  if(italic) return name + "-Italic";
  return name + "-Regular";
}

}

bool PdfFaces::load(PdfDocument& document) {
  // Every combination resolved to a slot, with the regular of the family as
  // the fallback -- so a build whose vendored tree is missing the italic still
  // exports, in the upright face, rather than refusing.
  int fallback[2] = {-1, -1};
  for(const bool mono : {false, true}) {
    for(const bool strong : {false, true}) {
      for(const bool italic : {false, true}) {
        const auto family = mono ? ui::FontFamily::Mono : ui::FontFamily::Sans;
        const auto path = ui::faceFile(family, strong, italic);
        int slot = -1;
        if(!path.empty()) {
          if(auto font = SfntFont::open(path)) {
            slot = document.addFont(std::move(*font), baseNameFor(mono, strong, italic));
          }
        }
        if(!mono && !strong && !italic) fallback[0] = slot;
        if(mono && !strong && !italic) fallback[1] = slot;
        slots_[static_cast<std::size_t>(index(mono, strong, italic))] = slot;
      }
    }
  }
  // Sans regular is the one face there is no exporting without: it sets the
  // running furniture and every fallback below points at it.
  if(fallback[0] < 0) return false;
  if(fallback[1] < 0) fallback[1] = fallback[0];
  for(const bool mono : {false, true}) {
    for(const bool strong : {false, true}) {
      for(const bool italic : {false, true}) {
        auto& slot = slots_[static_cast<std::size_t>(index(mono, strong, italic))];
        if(slot < 0) slot = fallback[mono ? 1 : 0];
      }
    }
  }
  loaded_ = true;
  return true;
}

int PdfFaces::slot(bool mono, bool strong, bool italic) const {
  if(!loaded_) return 0;
  return slots_[static_cast<std::size_t>(index(mono, strong, italic))];
}

int PdfFaces::slot(const doc::RunStyle& style) const {
  return slot(style.mono, style.strong, style.italic);
}

doc::TypeMetrics printTypeMetrics() {
  // The struct's own defaults are the screen scale, and taking them from there
  // rather than writing seven numbers out again is what keeps the printed
  // heading ladder the same ladder the reading pane has.
  const doc::TypeMetrics screen;
  doc::TypeMetrics print;
  print.body = std::round(screen.body * kPrintScale * 10.0f) / 10.0f;
  print.mono = std::round(screen.mono * kPrintScale * 10.0f) / 10.0f;
  for(int level = 0; level < 6; ++level) {
    print.heading[level] = std::round(screen.heading[level] * kPrintScale * 10.0f) / 10.0f;
  }
  print.lineHeightRatio = screen.lineHeightRatio;
  return print;
}

doc::LayoutOptions printLayoutOptions(float width) {
  const doc::LayoutOptions screen;
  doc::LayoutOptions print;
  print.width = width;
  print.fontScale = 1.0f;
  print.type = printTypeMetrics();
  print.indentStep = screen.indentStep * kPrintScale;
  print.listGutter = screen.listGutter * kPrintScale;
  print.quoteGutter = screen.quoteGutter * kPrintScale;
  print.blockSpacing = screen.blockSpacing * kPrintScale;
  print.headingSpaceAbove = screen.headingSpaceAbove * kPrintScale;
  return print;
}

doc::Metrics printMetrics(const PdfFaces& faces, PdfDocument& document) {
  doc::Metrics metrics;
  const doc::TypeMetrics type = printTypeMetrics();
  PdfDocument* doc = &document;
  const PdfFaces* set = &faces;
  metrics.measure = [doc, set](std::string_view value, const doc::RunStyle& style) {
    const float size = style.size > 0.0f ? style.size : printTypeMetrics().body;
    return doc->font(set->slot(style)).width(value, size);
  };
  // The same rule the screen uses: headings from h3 up are set tighter than
  // body copy, because a 1.5 ratio on a 20pt line is a gap you can park a
  // paragraph in.
  metrics.lineHeight = [type](const doc::RunStyle& style) {
    const float size = style.size > 0.0f ? style.size : type.body;
    const float ratio = size >= type.heading[2] ? 1.25f : type.lineHeightRatio;
    return std::round(size * ratio);
  };
  return metrics;
}

}
