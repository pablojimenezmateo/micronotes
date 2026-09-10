#include "ui/DocStyle.h"

namespace micronotes::ui {

TextStyle textStyleFor(const doc::RunStyle& style) {
  TextStyle out;
  out.family = style.mono ? FontFamily::Mono : FontFamily::Sans;
  out.strong = style.strong;
  out.italic = style.italic;
  out.size = style.size;
  return out;
}

SDL_Color inkFor(const Theme& theme, doc::TextRole role) {
  switch(role) {
    case doc::TextRole::Marker: return theme.textMuted;
    case doc::TextRole::Link:
    case doc::TextRole::WikiLink: return theme.accent;
    case doc::TextRole::WikiLinkUnresolved: return theme.linkPending;
    case doc::TextRole::ImageAlt:
    case doc::TextRole::Muted: return theme.textSecondary;
    case doc::TextRole::Code:
    case doc::TextRole::Body: break;
  }
  return theme.textPrimary;
}

SDL_Color inkFor(const Theme& theme, doc::TextRole role, doc::BlockKind kind) {
  const bool quoted = kind == doc::BlockKind::Quote || kind == doc::BlockKind::Callout;
  if(quoted && role == doc::TextRole::Body) return theme.textSecondary;
  return inkFor(theme, role);
}

}
