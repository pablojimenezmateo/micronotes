#include "app/PageViewStyle.h"

#include "ui/Fonts.h"
#include "ui/Theme.h"

namespace micronotes::app::pageview {

using ui::theme;

ui::TextStyle toTextStyle(const doc::RunStyle& style) {
  ui::TextStyle out;
  out.family = style.mono ? ui::FontFamily::Mono : ui::FontFamily::Sans;
  out.strong = style.strong;
  out.italic = style.italic;
  out.size = style.size;
  return out;
}

SDL_Color colorFor(doc::TextRole role) {
  switch(role) {
    case doc::TextRole::Marker: return theme().textMuted;
    case doc::TextRole::Link:
    case doc::TextRole::WikiLink: return theme().accent;
    case doc::TextRole::WikiLinkUnresolved: return theme().linkPending;
    case doc::TextRole::ImageAlt: return theme().textSecondary;
    case doc::TextRole::Muted: return theme().textSecondary;
    case doc::TextRole::Code: return theme().textPrimary;
    case doc::TextRole::Body: break;
  }
  return theme().textPrimary;
}

SDL_Color colorFor(doc::TextRole role, doc::BlockKind kind) {
  const bool quoted = kind == doc::BlockKind::Quote || kind == doc::BlockKind::Callout;
  if(quoted && role == doc::TextRole::Body) return theme().textSecondary;
  return colorFor(role);
}

ui::Rect toRect(const doc::Rect& rect, float originX, float originY) {
  return {rect.x + originX, rect.y + originY, rect.w, rect.h};
}

}
