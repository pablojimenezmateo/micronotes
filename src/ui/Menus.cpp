#include "ui/Menus.h"

#include "AppPerfCounters.h"
#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"
#include "core/util/StringUtil.h"
#include "ui/Metrics.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

constexpr MenuItemSpec sep() {
  return {ActionId::Count, "", true, false};
}

constexpr MenuItemSpec item(ActionId action, std::string_view label = {}, bool checkable = false) {
  return {action, label, false, checkable};
}

// The tables. Grouped the way a reader looks for things rather than the way the
// registry is ordered: what the note *is* (File), what the text *says* (Edit),
// how it is *shown* (View), where to *go*, and what to do to *this note*.
constexpr MenuItemSpec kFileItems[] {
  item(ActionId::NewNote),
  item(ActionId::NewFolder),
  sep(),
  item(ActionId::Save, "Save"),
  sep(),
  item(ActionId::RefreshLibrary),
  item(ActionId::RestoreFromTrash),
  sep(),
  item(ActionId::Settings),
  item(ActionId::Quit, "Quit"),
};

constexpr MenuItemSpec kEditItems[] {
  item(ActionId::Undo),
  item(ActionId::Redo),
  sep(),
  item(ActionId::Bold),
  item(ActionId::Italic),
  item(ActionId::Code),
  item(ActionId::Link, "Link"),
  sep(),
  item(ActionId::ToggleTask, "Tick task"),
  item(ActionId::TurnInto, "Turn into..."),
  item(ActionId::InsertBlock, "Insert block..."),
  item(ActionId::Fold, "Fold or unfold"),
  sep(),
  item(ActionId::DuplicateBlock, "Duplicate block"),
  item(ActionId::MoveBlockUp, "Move block up"),
  item(ActionId::MoveBlockDown, "Move block down"),
  item(ActionId::DeleteBlock, "Delete block"),
  sep(),
  item(ActionId::FindInNote, "Find..."),
  item(ActionId::FindNext, "Find next"),
  item(ActionId::FindPrevious, "Find previous"),
  item(ActionId::SearchAllNotes, "Search all notes..."),
};

constexpr MenuItemSpec kViewItems[] {
  item(ActionId::PaneLive, "Live", true),
  item(ActionId::PaneRaw, "Raw Markdown", true),
  item(ActionId::PaneReading, "Reading", true),
  item(ActionId::PaneSplit, "Split", true),
  item(ActionId::CyclePane, "Cycle views"),
  sep(),
  item(ActionId::ToggleSidebar, "Sidebar", true),
  item(ActionId::ToggleRightPanel, "Outline panel", true),
  item(ActionId::CycleRightPanel, "Cycle outline panel"),
  sep(),
  item(ActionId::ToggleTheme, "Light theme", true),
  sep(),
  item(ActionId::NextTab),
  item(ActionId::PreviousTab),
  item(ActionId::PinTab, "Pin tab", true),
  item(ActionId::CloseTab, "Close tab"),
};

constexpr MenuItemSpec kGoItems[] {
  item(ActionId::GoToNote, "Go to note..."),
  item(ActionId::CommandPalette, "Commands..."),
  sep(),
  item(ActionId::OpenInNewTab, "Open in a new tab..."),
};

constexpr MenuItemSpec kNoteItems[] {
  item(ActionId::RenameNote, "Rename..."),
  item(ActionId::SetNoteIcon, "Set icon..."),
  item(ActionId::EditTags, "Edit tags..."),
  item(ActionId::ToggleFavorite, "Favorite", true),
  sep(),
  item(ActionId::MoveNote, "Move to notebook..."),
  item(ActionId::MoveBlocks, "Move blocks to note..."),
  sep(),
  // The note as a file. A reader who wants the path to hand -- to paste into
  // another note, a commit message or a message to somebody -- should not have
  // to know that a right click on the tab has it.
  item(ActionId::ShowOnDisk, "Show on disk"),
  item(ActionId::CopyRelativePath, "Copy relative path"),
  item(ActionId::CopyAbsolutePath, "Copy absolute path"),
  sep(),
  item(ActionId::DeleteNote, "Delete note..."),
  sep(),
  item(ActionId::RenameFolder, "Rename notebook..."),
  item(ActionId::DeleteFolder, "Delete notebook..."),
};

constexpr MenuItemSpec kHelpItems[] {
  item(ActionId::Shortcuts, "Keyboard shortcuts..."),
  sep(),
  // The keys and the version are two halves of one page -- the shortcut list
  // *is* the About page -- but both are things people look for by name, so both
  // are offered by name and both land here.
  item(ActionId::About),
};

// The mnemonic is the initial in every case here, which is what makes the bar
// learnable: Alt and the letter you can see. `Note` and `Go` are the only pair
// that could have collided and do not, so nothing needs a second letter yet --
// and `menus_have_distinct_mnemonics` fails the build if a menu added later
// takes one that is already spoken for.
constexpr MenuSpec kMenus[] {
  {MenuId::File, "File", 'F', kFileItems},
  {MenuId::Edit, "Edit", 'E', kEditItems},
  {MenuId::View, "View", 'V', kViewItems},
  {MenuId::Go, "Go", 'G', kGoItems},
  {MenuId::Note, "Note", 'N', kNoteItems},
  {MenuId::Help, "Help", 'H', kHelpItems},
};

// The width a menu's own label asks for on the bar.
//
// Memoized, because the bar is laid out by the paint, by the hit test and by
// the cursor-shape pass, and all three run on every pointer motion event --
// six measures three times per motion, for a table that cannot change.
//
// Validity is the width of a probe string. The labels are static and the table
// is fixed, so the only thing that can move a measurement is the face itself,
// and one measure answers whether it has: a text-size change moves the probe
// exactly when it moves the labels. Cheaper than a generation counter the
// renderer would have to maintain and every caller would have to thread
// through, and it cannot be forgotten at a call site.
std::span<const float> menuLabelWidths(const MenuMeasureFn& measure) {
  static constexpr std::string_view kProbe = "Menu";
  static std::array<float, std::size(kMenus)> widths {};
  static float probe = -1.0f;

  const float current = static_cast<float>(measure(kProbe));
  if(std::abs(current - probe) > 0.01f) {
    probe = current;
    for(std::size_t i = 0; i < std::size(kMenus); ++i) {
      perf::addCounter(perf::CounterId::MenuBarLabelMeasures);
      widths[i] = static_cast<float>(measure(kMenus[i].label));
    }
  }
  return widths;
}

float barItemWidth(float labelWidth) {
  return std::clamp(labelWidth + kMenuBarItemPadding, kMenuBarItemMinWidth, kMenuBarItemMaxWidth);
}

}

std::span<const MenuSpec> menuSpecs() {
  return kMenus;
}

std::size_t menuMnemonicIndex(const MenuSpec& menu) {
  if(menu.mnemonic == 0) return std::string_view::npos;
  const char wanted = util::toLowerAscii(menu.mnemonic);
  for(std::size_t i = 0; i < menu.label.size(); ++i) {
    if(util::toLowerAscii(menu.label[i]) == wanted) return i;
  }
  return std::string_view::npos;
}

MenuId menuForMnemonic(char letter) {
  if(letter == 0) return MenuId::None;
  const char wanted = util::toLowerAscii(letter);
  for(const auto& menu : kMenus) {
    if(menu.mnemonic != 0 && util::toLowerAscii(menu.mnemonic) == wanted) return menu.id;
  }
  return MenuId::None;
}

const MenuSpec* findMenu(MenuId id) {
  for(const auto& menu : kMenus) {
    if(menu.id == id) return &menu;
  }
  return nullptr;
}

std::string_view menuItemLabel(const MenuItemSpec& item) {
  if(item.separator) return {};
  if(!item.label.empty()) return item.label;
  const ActionSpec* spec = findAction(item.action);
  return spec ? spec->label : std::string_view {};
}

std::string menuItemAccelerator(const MenuItemSpec& item) {
  if(item.separator) return {};
  const ActionSpec* spec = findAction(item.action);
  return spec ? acceleratorText(*spec) : std::string {};
}

Rect windowButtonHitRect(Rect button) {
  if(empty(button)) return button;
  return {button.x - kWindowButtonHitInflate, button.y - kWindowButtonHitInflate,
          button.w + kWindowButtonHitInflate * 2.0f, button.h + kWindowButtonHitInflate * 2.0f};
}

MenuBarLayout menuBarLayout(Rect menuBar, MenuId openMenu, bool customChrome,
                            const MenuMeasureFn& measure) {
  perf::addCounter(perf::CounterId::MenuBarLayouts);
  MenuBarLayout layout;
  if(menuBar.w <= 0.0f || menuBar.h <= 0.0f) return layout;

  // The controls first, right to left, because the menus lay out against
  // whatever they leave. Square at whatever height the bar has room for, so
  // the three of them scale with the bar rather than against it.
  float availableRight = menuBar.x + menuBar.w - kMenuBarEdgeInset;
  if(customChrome) {
    const float size = std::max(18.0f, menuBar.h - kMenuBarItemInsetY * 2.0f);
    const float total = size * 3.0f + kWindowButtonGap * 2.0f;
    const float startX = menuBar.x + std::max(0.0f, menuBar.w - total - kWindowButtonRightInset);
    const float y = std::round(menuBar.y + (menuBar.h - size) / 2.0f);
    for(std::size_t i = 0; i < layout.windowButtons.size(); ++i) {
      layout.windowButtons[i] = {startX + (size + kWindowButtonGap) * static_cast<float>(i), y,
                                 size, size};
    }
    availableRight = layout.windowButtons.front().x - kMenuBarEdgeInset;
  }

  const std::span<const float> labels = menuLabelWidths(measure);
  const float y = menuBar.y + kMenuBarItemInsetY;
  const float height = std::max(18.0f, menuBar.h - kMenuBarItemInsetY * 2.0f);

  // Does the whole bar fit? The trailing gap after the last item is not real
  // -- n items have n-1 gaps between them -- so it comes off before the
  // comparison, or a bar that exactly fits would spuriously overflow.
  float wanted = menuBar.x + kMenuBarEdgeInset;
  for(std::size_t i = 0; i < std::size(kMenus); ++i) {
    wanted += barItemWidth(labels[i]) + kMenuBarItemGap;
  }
  wanted -= kMenuBarItemGap;
  const bool overflows = wanted > availableRight;
  const float limit = overflows ? availableRight - (kMenuOverflowChevronWidth + kMenuBarItemGap)
                                : availableRight;

  float x = menuBar.x + kMenuBarEdgeInset;
  std::size_t placed = 0;
  for(std::size_t i = 0; i < std::size(kMenus); ++i) {
    const float width = barItemWidth(labels[i]);
    if(x + width > limit) break;
    layout.items.push_back({kMenus[i].id, {x, y, width, height}, kMenus[i].id == openMenu});
    x += width + kMenuBarItemGap;
    ++placed;
  }
  for(std::size_t i = placed; i < std::size(kMenus); ++i) {
    layout.overflow.push_back(kMenus[i].id);
  }
  if(!layout.overflow.empty()) {
    layout.chevron = {availableRight - kMenuOverflowChevronWidth, y, kMenuOverflowChevronWidth,
                      height};
  }
  return layout;
}

Rect menuPopupRect(Rect anchor, std::span<const MenuItemSpec> items, Rect bounds,
                   const MenuMeasureFn& measure) {
  if(items.empty()) return {};
  perf::addCounter(perf::CounterId::MenuPopupLayouts);

  float wanted = kMenuPopupMinWidth;
  float height = kMenuPopupPadY * 2.0f;

  for(const MenuItemSpec& spec : items) {
    height += menuRowHeight(spec.separator);
    // A rule has no label and no accelerator, so it asks the popup for no width.
    if(spec.separator) continue;
    const std::string accelerator = menuItemAccelerator(spec);
    const float row = static_cast<float>(measure(menuItemLabel(spec))) +
                      static_cast<float>(measure(accelerator)) + kMenuPopupTextReserve;
    wanted = std::max(wanted, row);
  }

  const float maxWidth = std::max(kMenuPopupMinWidth, bounds.w - kSpace2);
  const float width = std::clamp(wanted, kMenuPopupMinWidth, maxWidth);
  // A menu taller than the window it opens in is capped rather than allowed to
  // hang off the bottom. It costs the tail of the longest menu on a very short
  // window, which is the least bad of the three options: hanging off is items
  // nobody can see *or* reason about, and scrolling a menu bar's popup is a
  // device no desktop has.
  height = std::min(height, std::max(kMenuPopupItemHeight, bounds.h - kSpace1 * 2.0f));
  const float x = std::clamp(anchor.x, bounds.x + kSpace1,
                             bounds.x + std::max(kSpace1, bounds.w - width - kSpace1));
  // Overlapping the anchor's last pixel, so the open menu and its popup read as
  // one shape rather than as a button with a list under it.
  float y = anchor.y + std::max(0.0f, anchor.h) - 1.0f;
  if(y + height > bounds.y + bounds.h - kSpace1) y = anchor.y - height + 1.0f;
  y = std::clamp(y, bounds.y + kSpace1, bounds.y + std::max(kSpace1, bounds.h - height - kSpace1));
  return {std::round(x), std::round(y), width, height};
}

RowCursor menuPopupRows(Rect popup) {
  return RowCursor(popup.x, popup.w, popup.y + kMenuPopupPadY);
}

Rect menuPopupItemRect(Rect popup, std::span<const MenuItemSpec> items, std::size_t index) {
  if(index >= items.size()) return {};
  RowCursor rows = menuPopupRows(popup);
  Rect row {};
  for(std::size_t i = 0; i <= index; ++i) row = rows.place(menuRowHeight(items[i].separator));
  return row;
}

std::optional<std::size_t> menuPopupItemAt(Rect popup, std::span<const MenuItemSpec> items,
                                           float x, float y) {
  if(!contains(popup, x, y)) return std::nullopt;
  // One walk. This used to call `menuPopupItemRect` per item, each of which
  // re-stacked the popup from the top -- quadratic, on a path that runs for
  // every pointer motion event over an open menu.
  RowCursor rows = menuPopupRows(popup);
  for(std::size_t i = 0; i < items.size(); ++i) {
    const Rect row = rows.place(menuRowHeight(items[i].separator));
    if(items[i].separator) continue;
    if(contains(row, x, y)) return i;
  }
  return std::nullopt;
}

}
