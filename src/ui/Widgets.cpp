#include "ui/Widgets.h"

#include "CoreAliases.h"
#include "core/editor/SoftWrap.h"
#include "core/render/ColorMath.h"
#include "ui/Glyphs.h"
#include "ui/Metrics.h"
#include "ui/Painter.h"
#include "ui/TextFit.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace micronotes::ui {

void drawTextFieldFrame(SDL_Renderer* renderer, Rect box, bool active) {
  fill(renderer, box, theme().surfaceBackground);
  stroke(renderer, box, active ? theme().accent : theme().border);
}

void drawButton(SDL_Renderer* renderer, TextRenderer& text, Rect box, std::string_view label,
                bool enabled, bool hovered, ButtonTone tone) {
  SDL_Color fillColor = theme().surfaceRaised;
  SDL_Color borderColor = theme().border;
  SDL_Color ink = theme().textPrimary;
  if(tone == ButtonTone::Accent) {
    fillColor = theme().accent;
    borderColor = theme().accent;
    ink = theme().onAccent;
  } else if(tone == ButtonTone::Destructive) {
    fillColor = theme().warn;
    borderColor = theme().warn;
    ink = theme().onAccent;
  }
  if(!enabled) {
    fillColor = theme().surfaceBackground;
    borderColor = theme().border;
    ink = theme().textDisabled;
  } else if(hovered) {
    // Toward the ink rather than toward a second fill: one blend serves all
    // three tones, where a hover colour per tone is three more constants that
    // have to be kept in step with the three they hover over.
    fillColor = render::blend(fillColor, ink, 0.14f);
  }
  fill(renderer, box, fillColor);
  stroke(renderer, box, borderColor);

  const TextStyle style = chromeStyle();
  const float width = static_cast<float>(text.width(label, style));
  text.draw(label, std::round(box.x + (box.w - width) / 2.0f), textTop(box, text, style), ink, style);
}

Rect drawTitledCard(SDL_Renderer* renderer, Rect card, float headerHeight) {
  drawSurface(renderer, card, theme().overlayBackground, theme().border);
  const Rect header {card.x, card.y, card.w, std::max(0.0f, headerHeight)};
  if(header.h <= 0.0f) return header;
  // The chrome ground, so the header reads as the same kind of surface as the
  // menu bar and the tab strip rather than as the first row of whatever is
  // under it.
  fill(renderer, header, theme().chromeBackground);
  fill(renderer, {header.x, header.y + header.h - kDividerThickness, header.w, kDividerThickness},
       theme().border);
  return header;
}

void drawSectionBand(SDL_Renderer* renderer, TextRenderer& text, Rect band, Rect chevron,
                     std::string_view label, std::string_view trailing, bool collapsed,
                     bool hovered, float trailingReserve) {
  const bool collapsible = chevron.w > 0.0f;
  // A step up out of the panel, which is what makes it a band. Hover lifts it
  // one further, but only when there is something to shut: a caption that
  // brightens under the pointer is a caption promising a click it will not
  // honour.
  fill(renderer, band, hovered && collapsible ? theme().rowHighlight : theme().surfaceRaised);
  // The rule goes along the *top*, so it separates this band from the group
  // above rather than from the rows it heads -- those belong to it.
  fill(renderer, {band.x, band.y, band.w, kDividerThickness}, theme().border);

  // The chrome size, not a step down from it. At 0.85 of it the label came out
  // 11px, and 11px uppercase in a mono face with `textSecondary`'s ink has
  // stems the rasterizer spreads over two columns at partial coverage -- so the
  // headings read as blurred next to the 13px rows they head, which is the one
  // thing a heading must not do. microide's section headers are set at its
  // single chrome size for the same reason; what distinguishes a band from a
  // row is its ground and its outdent, never a smaller type.
  const TextStyle style = chromeStyle();
  float right = band.x + band.w - kSidebarInset - trailingReserve;
  if(!trailing.empty()) {
    const float width = static_cast<float>(text.width(trailing, style));
    text.draw(trailing, right - width, textTop(band, text, style), theme().textMuted, style);
    right -= width + kSpace2;
  }
  // Outdented an indent step ahead of the rows under it. See `kSectionLabelX`.
  const float labelX = band.x + kSectionLabelX;
  text.draw(ellipsizeToWidth(text, std::string(label), static_cast<int>(std::max(0.0f, right - labelX)), style),
            labelX, textTop(band, text, style),
            hovered && collapsible ? theme().textPrimary : theme().textSecondary, style);
  if(collapsible) {
    drawChevron(renderer, chevron.x, chevron.y + chevron.h / 2.0f, !collapsed,
                hovered ? theme().textPrimary : theme().textMuted);
  }
}

float drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail,
                       float x, float y, float width, std::string_view keys) {
  const TextStyle titleStyle {FontFamily::Sans, true, false, type().ui};
  const TextStyle bodyStyle {FontFamily::Sans, false, false, type().small};
  const TextStyle keyStyle {FontFamily::Sans, false, false, type().tiny};
  const int room = static_cast<int>(std::max(60.0f, width - 36.0f));
  const float top = y;
  y += 14.0f;
  text.draw(ellipsizeToWidth(text, std::string(title), room, titleStyle), x + 18.0f, y, theme().textPrimary, titleStyle);
  y += static_cast<float>(text.lineHeight(titleStyle)) + 6.0f;

  // The detail wraps rather than being cut off at the column. Every one of
  // these messages is drawn in a panel -- the sidebar, the outline -- narrow
  // enough that a sentence does not fit on one line, and half a sentence with
  // an ellipsis after it says less than nothing: "Notes here are plain .md..."
  // was the whole of what a fresh library had to say for itself.
  //
  // Bounded, because the box is: past three lines the message is no longer an
  // empty state, and the last of them takes the ellipsis instead.
  static constexpr std::size_t kMaxLines = 3;
  const auto rows = editor::softWrap(detail, room, [&](std::string_view value) {
    return text.width(value, bodyStyle);
  });
  const float bodyStep = static_cast<float>(text.lineHeight(bodyStyle));
  for(std::size_t i = 0; i < rows.size() && i < kMaxLines; ++i) {
    const bool last = i + 1 == kMaxLines && rows.size() > kMaxLines;
    text.draw(last ? ellipsizeToWidth(text, rows[i].text + "...", room, bodyStyle) : rows[i].text,
              x + 18.0f, y, theme().textSecondary, bodyStyle);
    y += bodyStep;
  }

  if(!keys.empty()) {
    y += 8.0f;
    text.draw(ellipsizeToWidth(text, std::string(keys), room, keyStyle), x + 18.0f, y, theme().textMuted, keyStyle);
    y += static_cast<float>(text.lineHeight(keyStyle));
  }
  return y + 14.0f - top;
}

void drawTooltip(SDL_Renderer* renderer, TextRenderer& text, const HoverTooltip& tooltip, Rect bounds) {
  if(!tooltip.showing()) return;
  const TextStyle style {FontFamily::Sans, false, false, type().small};
  const float width = static_cast<float>(text.width(tooltip.text, style)) + kTooltipPadX * 2.0f;
  const float height = static_cast<float>(text.lineHeight(style)) + kTooltipPadY * 2.0f;
  const Rect card = placeTooltip(tooltip.anchor, width, height, bounds);
  drawSurface(renderer, card, theme().overlayBackground, theme().border);
  text.draw(tooltip.text, card.x + kTooltipPadX, card.y + kTooltipPadY, theme().textPrimary, style);
}

void drawMenuRow(SDL_Renderer* renderer, TextRenderer& text, Rect row, const MenuRow& item) {
  if(item.highlighted && item.enabled) fill(renderer, row, theme().rowHighlight);
  const TextStyle style = chromeStyle();
  const TextStyle small = chromeSmallStyle();
  const SDL_Color ink = !item.enabled ? theme().textDisabled
                      : item.destructive ? theme().warn
                      : item.highlighted ? theme().textPrimary
                                         : theme().textSecondary;
  const SDL_Color trailing = item.enabled ? theme().textMuted : theme().textDisabled;
  if(item.checked) {
    drawCheckGlyph(renderer, {row.x + kSpace2, row.y + 3.0f, 10.0f, std::max(0.0f, row.h - 6.0f)},
                   item.enabled ? theme().accent : theme().textDisabled);
  }
  const float baseline = textTop(row, text, style);

  // Both trailing pieces are placed right to left against a running edge. A
  // fixed gap between them only works while the accelerator is short, and a
  // deletion timestamp is not.
  float right = row.x + row.w - kMenuPopupAcceleratorInset;
  float labelRoom = row.w - kMenuPopupLabelInset - kMenuPopupAcceleratorInset - kSpace2;
  if(!item.accelerator.empty()) {
    const auto width = static_cast<float>(text.width(item.accelerator, style));
    text.draw(item.accelerator, right - width, baseline, trailing, style);
    right -= width + kSpace3;
    labelRoom -= width;
  }
  if(!item.detail.empty()) {
    // Cut to half the room rather than to all of it: a detail that crowded the
    // label out would hide the thing being chosen in favour of a note about it.
    const float detailBaseline = textTop(row, text, small);
    const auto detail = ellipsizeToWidth(text, std::string(item.detail),
                                         static_cast<int>(labelRoom / 2.0f), small);
    const auto width = static_cast<float>(text.width(detail, small));
    text.draw(detail, right - width, detailBaseline, theme().textMuted, small);
    labelRoom -= width + kSpace3;
  }
  text.draw(ellipsizeToWidth(text, std::string(item.label), static_cast<int>(labelRoom), style),
            row.x + kMenuPopupLabelInset, baseline, ink, style);
}

void drawMenuSeparator(SDL_Renderer* renderer, Rect row) {
  hLine(renderer, row.x + kSpace2, row.x + row.w - kSpace2, std::round(row.y + row.h / 2.0f),
        theme().border);
}

StripTabColors stripTabColors() {
  return {
    theme().chromeActive,
    // A step *up* from the strip's own ground, not level with it. An inactive
    // tab filled with the strip's colour is not a tab: with two of them side by
    // side the only thing saying where one ended was the 1px rule between them,
    // so a strip of three notes read as one wide empty band with some words in
    // it. The active tab is then a further step up, plus its accent lid.
    theme().surfaceRaised,
    theme().rowHighlight,
    theme().chromeActiveText,
    theme().chromeTextSecondary,
  };
}

void drawStripTab(SDL_Renderer* renderer, TextRenderer& text, Rect rect, std::string_view label,
                  bool active, bool hovered, float closeReserve, const StripTabColors& colors) {
  const SDL_Color background = active ? colors.activeFill
                             : hovered ? colors.hoverFill
                                       : colors.inactiveFill;
  fill(renderer, rect, background);
  // The lid, not an outline: it says which tab the page below belongs to, and
  // an outline would say "this tab is selected" about a strip where exactly one
  // tab is always selected.
  if(active) fill(renderer, {rect.x, rect.y, rect.w, kRowAccentWidth}, theme().accent);
  // A rule between one tab and the next, so two inactive neighbours do not read
  // as one wide tab. Skipped on the active one, which its own fill separates.
  if(!active) {
    fill(renderer, {rect.x + rect.w - kDividerThickness, rect.y + kSpace1, kDividerThickness,
                    rect.h - kSpace1 * 2.0f}, theme().border);
  }

  const TextStyle style = chromeStyle();
  const float left = rect.x + kSidebarInset;
  const int room = static_cast<int>(rect.x + rect.w - closeReserve - left);
  text.draw(ellipsizeToWidth(text, std::string(label), room, style), left,
            textTop(rect, text, style), active ? colors.activeText : colors.inactiveText, style);
}

void drawStripOverflowButton(SDL_Renderer* renderer, TextRenderer& text, Rect box,
                             bool pointRight, std::size_t hidden, bool hovered) {
  if(hidden == 0 || empty(box)) return;
  const SDL_Color background = hovered ? theme().rowHighlight : theme().chromeBackground;
  const SDL_Color ink = hovered ? theme().textPrimary : theme().chromeTextSecondary;
  fill(renderer, box, background);
  stroke(renderer, box, theme().border);
  drawArrowGlyph(renderer, {box.x, box.y, 16.0f, box.h}, pointRight, ink);

  const TextStyle style = chromeSmallStyle();
  const std::string count = std::to_string(hidden);
  const float width = static_cast<float>(text.width(count, style));
  if(width + 18.0f > box.w) return;
  text.draw(count, box.x + box.w - width - kSpace1, textTop(box, text, style), ink, style);
}

}
