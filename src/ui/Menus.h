#pragma once

#include "ui/Actions.h"
#include "ui/Rect.h"
#include "ui/RowCursor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// The menu bar, as a table rather than as draw code.
//
// The bar is the shell's fourth view of `ActionId`, beside the palette, the
// shortcut list and the key handler. It carries no commands of its own: an
// item is an action, its accelerator comes from the action's chord, whether it
// is greyed comes from the action's `needsNote`, and running it goes through
// the same `performCommand` the palette calls. That is deliberate -- the icon
// rail this replaces had its own table, and a rail control could be clicked on
// with nothing behind it.
enum class MenuId : std::uint8_t {
  None,
  File,
  Edit,
  View,
  Go,
  Tabs,
  Note,
  Help
};

struct MenuItemSpec {
  ActionId action = ActionId::Count;
  // Empty means "the action's own label". Overridden where the palette's
  // wording is wrong for a menu: a palette row has to stand alone and says
  // "Show or hide the sidebar", where the View menu already provides the verb
  // and wants "Sidebar" with a tick beside it.
  std::string_view label;
  bool separator = false;
  // A tick rather than a verb: the item reports a state the shell is already
  // in. Whether it is *currently* ticked is a question about the shell, not
  // about the table, so the draw asks -- see `app::menuItemChecked`.
  bool checkable = false;
};

struct MenuSpec {
  MenuId id = MenuId::None;
  std::string_view label;
  // The letter that opens this menu from the keyboard, and the one the bar
  // underlines. Zero for a menu with none.
  //
  // A field rather than an `&File` marker in the label, because the label is
  // read by the layout, the measure and the draw, and every one of them would
  // have had to know to strip a sigil -- the one that forgot would put an
  // ampersand on the bar or measure the wrong width. The sibling's `MenuSpec`
  // does not model this at all, so there was nothing to copy.
  char mnemonic = 0;
  std::span<const MenuItemSpec> items;
};

std::span<const MenuSpec> menuSpecs();
const MenuSpec* findMenu(MenuId id);

// Where the mnemonic letter sits in a menu's own label, or `npos`.
//
// The underline goes under the letter *in the label*, so a mnemonic the label
// does not contain draws nothing rather than guessing. Case-insensitive, and
// the first occurrence wins.
std::size_t menuMnemonicIndex(const MenuSpec& menu);

// The menu `letter` opens, or `MenuId::None`. Case-insensitive.
MenuId menuForMnemonic(char letter);

// What a row prints: the override if the table gave one, otherwise the
// action's label. A separator has neither.
std::string_view menuItemLabel(const MenuItemSpec& item);
std::string menuItemAccelerator(const MenuItemSpec& item);

// Measuring a string in the font the bar is drawn in. The layout is otherwise
// free of the renderer, which is what lets it be checked without a window --
// but the widths depend on the face, so a caller that omits this lays out a
// *different* bar from the one that gets painted.
using MenuMeasureFn = std::function<int(std::string_view)>;

struct MenuBarItem {
  MenuId id = MenuId::None;
  Rect rect;
  bool active = false;

  friend bool operator==(const MenuBarItem&, const MenuBarItem&) = default;
};

// Everything on the bar, in one pass.
//
// One function rather than microide's three, which each re-derived the item
// widths and then had to be memoized against each other: the paint, the hit
// test and the cursor-shape pass all want the whole bar, and all three run on
// every pointer motion. Asking once means the three cannot disagree about
// where the chevron starts.
struct MenuBarLayout {
  std::vector<MenuBarItem> items;
  // The menus that did not fit, in table order. They are reachable through the
  // chevron rather than dropped.
  std::vector<MenuId> overflow;
  // Empty when nothing overflowed.
  Rect chevron;
  // Minimise, maximise, close, in that order and left to right so close ends
  // nearest the corner. All three empty when the compositor draws the frame.
  std::array<Rect, 3> windowButtons {};
};

MenuBarLayout menuBarLayout(Rect menuBar, MenuId openMenu, bool customChrome,
                            const MenuMeasureFn& measure);

// Where an open menu's popup goes, given the bar item it hangs off and the
// window it has to stay inside. Flipped above the anchor when it would fall
// off the bottom, which is what a menu bar along the *bottom* of a short
// window needs and costs nothing here.
Rect menuPopupRect(Rect anchor, std::span<const MenuItemSpec> items, Rect bounds,
                   const MenuMeasureFn& measure);

// The cursor a popup's rows are placed with: the card's own column, starting
// under its top padding, advancing by `menuRowHeight` per item.
//
// The paint, the hit test and `menuPopupItemRect` all take one of these, and
// that is the point. Where a popup's rows go was arithmetic written out at each
// of the three, and a disagreement between the paint and the hit test is a
// *wrong command run*, not a wrong pixel.
RowCursor menuPopupRows(Rect popup);

// The row `index` occupies inside that popup. Separators have a row too -- a
// short one -- so the index into this and the index into `items` are the same
// number, and a hit test cannot land one item out.
Rect menuPopupItemRect(Rect popup, std::span<const MenuItemSpec> items, std::size_t index);

// Which item is under a point, or nothing: outside the popup, over a
// separator, or over the padding at either end.
std::optional<std::size_t> menuPopupItemAt(Rect popup, std::span<const MenuItemSpec> items,
                                           float x, float y);

// The grab region of a window control. Larger than the button drawn in it, for
// the reason the tab close cross is: one constant, read by the paint and the
// hit test alike.
Rect windowButtonHitRect(Rect button);

}
