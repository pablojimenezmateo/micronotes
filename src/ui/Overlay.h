#pragma once

#include "ui/TextField.h"
#include "ui/Draw.h"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace micronotes::ui {

enum class OverlayKind {
  // A single-line text field with a title, e.g. rename or tag editing.
  TextPrompt,
  // A list of choices, optionally filtered as the user types. Backs context
  // menus, the slash menu, and the command palette.
  List,
  // A destructive-action confirmation.
  Confirm
};

struct OverlayItem {
  std::string id;
  std::string label;
  std::string detail;     // secondary line, e.g. a folder path
  std::string shortcut;   // right-aligned hint, e.g. "Ctrl+N"
  bool enabled = true;
  bool destructive = false;
};

struct Overlay {
  OverlayKind kind = OverlayKind::List;
  std::string id;         // identifies the overlay when a result comes back
  std::string title;
  std::string hint;

  // TextPrompt value, or the filter text of a filterable List.
  TextField value;
  std::string placeholder;

  std::vector<OverlayItem> items;
  bool filterable = false;
  int highlighted = 0;
  // First visible row, as a position in the filtered list rather than an item
  // index: the filter reorders items, and a scroll offset that meant an item
  // would jump every time the text changed.
  int scroll = 0;
  // Most rows this overlay wants on screen at once: enough for the whole
  // block-type list before anyone filters it. A menu is better short; a
  // reference list asks for more. Capped by what the window can fit.
  int maxRows = 12;

  std::string confirmLabel = "Confirm";

  // Anchored overlays hang off a point (context menus); otherwise the overlay
  // is centred horizontally near the top of the window, like a palette.
  bool anchored = false;
  float anchorX = 0.0f;
  float anchorY = 0.0f;
  float width = 340.0f;
};

struct OverlayResult {
  std::string overlayId;
  std::string itemId;   // List/Confirm: the chosen item
  std::string value;    // TextPrompt: the entered text
};

class OverlayStack {
public:
  bool active() const;
  const Overlay* top() const;
  Overlay* top();

  void open(Overlay overlay);
  void close();
  void closeAll();

  // Each handler returns a result once the user commits; nullopt otherwise.
  // `handled` reports whether the overlay consumed the event at all, so the
  // caller knows not to route it to the rest of the application.
  std::optional<OverlayResult> handleKey(SDL_Keycode key, bool ctrl, bool shift, bool& handled);
  bool handleText(const char* input);
  std::optional<OverlayResult> handleClick(float x, float y, bool& handled);
  void handleMotion(float x, float y);
  // Returns whether an overlay took the wheel, so it cannot also scroll the
  // note behind it.
  bool handleWheel(float dy);

  void draw(SDL_Renderer* renderer, TextRenderer& text, int windowWidth, int windowHeight);

private:
  struct Layout {
    Rect panel;
    Rect field;
    std::vector<Rect> itemRects;
    std::vector<int> itemIndices;  // into Overlay::items
  };

  Layout layoutFor(const Overlay& overlay, TextRenderer& text, int windowWidth, int windowHeight) const;
  std::vector<int> visibleIndices(const Overlay& overlay) const;
  std::optional<OverlayResult> commit();
  void moveHighlight(int delta);
  // A list longer than the panel scrolls to follow the highlight, so arrowing
  // past the last visible row cannot lose it off the bottom.
  void ensureHighlightVisible();
  void resetHighlight();

  std::vector<Overlay> stack_;
  Layout lastLayout_;
  // Rows the last layout actually fitted. Scrolling has to agree with what was
  // drawn, and only the layout knows how tall the window was.
  mutable int lastRows_ = 12;
  float mouseX_ = -1.0f;
  float mouseY_ = -1.0f;
};

}
