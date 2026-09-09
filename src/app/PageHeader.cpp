#include "app/PageHeader.h"

#include "app/Shell.h"
#include "app/SidebarModel.h"

#include "ui/Fonts.h"
#include "ui/Metrics.h"
#include "ui/NoteProperties.h"
#include "ui/TagColors.h"
#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::fill;
using ui::hLine;
using ui::theme;

// Air above the properties and below the lot.
constexpr float kSpaceAboveProperties = 6.0f;
constexpr float kPropertyRowHeight = 24.0f;
constexpr float kSpaceBelowHeader = 18.0f;
// How much of the column the key column takes. Wide enough for the keys people
// actually write -- `aliases`, `created`, `publish` -- and capped so a note with
// one absurd key does not push every value off the right of the page.
constexpr float kKeyColumnFraction = 0.26f;
constexpr float kKeyColumnMax = 160.0f;

ui::TextStyle keyStyle() {
  return ui::TextStyle {ui::FontFamily::Sans, false, false, ui::type().small};
}

ui::TextStyle valueStyle() {
  return ui::TextStyle {ui::FontFamily::Sans, false, false, ui::type().ui};
}

// The header for the note that is open, read from the file at most once per
// note per library revision.
//
// Rebuilt on a key rather than by a flag raised at every mutation site: a flag
// has to be raised in rename, in the tag editor, in the icon prompt and in
// whatever is added next, and the one place it is forgotten leaves the page
// naming a note by the title it used to have. A key cannot be forgotten, only
// unequal. Rename, tags and icon all go through refreshLibrary(), which is
// where the revision moves.
const std::vector<ui::NoteProperty>& refreshedHeader(UiRuntime& ui) {
  const NoteRevision key {ui.state.selection().noteId, ui.state.catalog().revision()};
  if(const auto* rows = ui.pageHeader.get(key)) return *rows;

  auto& rows = ui.pageHeader.rebuild(key);
  rows.clear();
  if(key.noteId.empty() || !ui.state.catalog().isOpen()) return rows;
  // From the open-note record, which costs nothing. This used to be
  // `selectedNote()`: a read and a front-matter parse of the whole note, keyed
  // on the library revision -- which moves on every save. Drawing the header
  // re-read the note once a second while somebody was typing into it.
  const auto& note = ui.state.openNote();
  if(note.noteId().empty()) return rows;
  rows = ui::notePropertiesOf(note.metadata());
  return rows;
}

float propertiesHeight(const std::vector<ui::NoteProperty>& rows) {
  if(rows.empty()) return 0.0f;
  return static_cast<float>(rows.size()) * kPropertyRowHeight + ui::kSpace2;
}

}

float pageHeaderHeight(ui::TextRenderer& text, UiRuntime& ui) {
  (void)text;
  const auto& rows = refreshedHeader(ui);
  if(rows.empty()) return 0.0f;
  return kSpaceAboveProperties + propertiesHeight(rows) + kSpaceBelowHeader;
}

void drawPageHeader(ui::TextRenderer& text, UiRuntime& ui, Rect column, float top) {
  const auto& rows = refreshedHeader(ui);
  if(rows.empty()) return;

  float y = top + kSpaceAboveProperties;

  const auto key = keyStyle();
  const auto value = valueStyle();
  const float keyColumn = std::min(kKeyColumnMax, column.w * kKeyColumnFraction);
  const float valueLeft = column.x + keyColumn;
  const float valueRoom = std::max(40.0f, column.w - keyColumn);
  for(const auto& row : rows) {
    const float keyBaseline = y + (kPropertyRowHeight - static_cast<float>(text.lineHeight(key))) / 2.0f;
    text.draw(ui::ellipsizeToWidth(text, row.key, static_cast<int>(keyColumn - ui::kSpace3), key),
              column.x, keyBaseline, theme().textMuted, key);
    const float baseline = y + (kPropertyRowHeight - static_cast<float>(text.lineHeight(value))) / 2.0f;
    text.draw(ui::ellipsizeToWidth(text, row.value, static_cast<int>(valueRoom), value),
              valueLeft, baseline, theme().textSecondary, value);
    y += kPropertyRowHeight;
  }
}

}
