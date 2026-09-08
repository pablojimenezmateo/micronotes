#pragma once

#include "CoreAliases.h"

#include "app/PageView.h"
#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"
#include "core/editor/TextField.h"
#include "core/markdown/MarkdownParser.h"
#include "core/platform/DirectoryWatcher.h"
#include "app/NoteCaches.h"
#include "app/PanelState.h"
#include "app/PointerState.h"
#include "app/ChromeState.h"
#include "app/EditingState.h"
#include "app/RawPaneState.h"
#include "app/TextFields.h"
#include "app/SidebarState.h"
#include "app/TabStrip.h"
#include "app/Wheel.h"
#include "doc/BlockScan.h"
#include "library/Library.h"
#include "ui/AppState.h"
#include "ui/Draw.h"
#include "ui/NoteProperties.h"
#include "ui/Outline.h"
#include "ui/FoldState.h"
#include "core/util/Hash.h"
#include "ui/Actions.h"
#include "ui/CaretBlink.h"
#include "ui/Menus.h"
#include "ui/Overlay.h"
#include "ui/Memo.h"
#include "ui/Rect.h"
#include "ui/Settings.h"
#include "ui/SettingsSurface.h"
#include "ui/ShellLayout.h"

#include "ui/Tooltip.h"
#include "ui/TreeModel.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cmath>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Everything the running shell knows, as the composition of the things that
// own it.
//
// It lived in Application.cpp's anonymous namespace first, which meant no other
// translation unit could name it -- and so nothing could be moved out of that
// file no matter how large it grew. Hoisting it into a header is what made the
// decomposition possible, and for a while it was a hundred and thirty loose
// fields in one struct: the whole shell's state in declaration order, with
// nothing saying which surface owned what, and every question about the sidebar
// or the caret or the menu bar answered by reading all of it.
//
// Now each surface's state is a named type in its own header, and this is the
// composition. Two things follow that did not before. A function can take the
// part it works on rather than the whole shell, which is what makes it
// testable. And a rule about one surface's state can be a *method* on that
// surface's type -- `SidebarDrag::active`, `RightPanelState::rebaseScroll`,
// `ClickRun::extend` -- instead of a line of arithmetic repeated at each of its
// call sites, which is where the shell's quieter bugs came from.
//
// What is left directly on the runtime is what is genuinely shell-wide: the
// library, the buffer, where the focus is, and what the status line says.
namespace micronotes::app {

using micronotes::ui::Rect;
using micronotes::ui::ShellLayout;
using micronotes::ui::TextRenderer;

enum class FocusArea {
  Folders,
  Editor,
  Search,
  Find,
  Viewer,
  TagEditor,
  RenameNote,
  RenameFolder
};

enum class CursorKind {
  Default,
  Text,
  Pointer,
  ResizeHorizontal,
  ResizeVertical
};

inline const char* focusName(FocusArea focus) {
  switch(focus) {
    case FocusArea::Folders: return "Folders";
    case FocusArea::Editor: return "Editor";
    case FocusArea::Search: return "Search";
    case FocusArea::Find: return "Find";
    case FocusArea::Viewer: return "Viewer";
    case FocusArea::TagEditor: return "TagEditor";
    case FocusArea::RenameNote: return "RenameNote";
    case FocusArea::RenameFolder: return "RenameFolder";
  }
  return "Unknown";
}

inline bool inputDebugEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("MICRONOTES_DEBUG_INPUT");
    return value && *value && std::string_view(value) != "0";
  }();
  return enabled;
}

struct LinkRegion {
  Rect rect;
  std::string target;
  // See PageLink::wiki: a link to a note is followed differently from a link
  // to a file, and the two are indistinguishable once they are just strings.
  bool wiki = false;
};

struct SystemCursors {
  SDL_Cursor* defaultCursor = nullptr;
  SDL_Cursor* text = nullptr;
  SDL_Cursor* pointer = nullptr;
  SDL_Cursor* resizeHorizontal = nullptr;
  SDL_Cursor* resizeVertical = nullptr;
  CursorKind active = CursorKind::Default;

  bool init() {
    defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    resizeHorizontal = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    resizeVertical = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    if(!defaultCursor || !text || !pointer || !resizeHorizontal || !resizeVertical) return false;
    SDL_SetCursor(defaultCursor);
    return true;
  }

  void destroy() {
    if(defaultCursor) SDL_DestroyCursor(defaultCursor);
    if(text) SDL_DestroyCursor(text);
    if(pointer) SDL_DestroyCursor(pointer);
    if(resizeHorizontal) SDL_DestroyCursor(resizeHorizontal);
    if(resizeVertical) SDL_DestroyCursor(resizeVertical);
    defaultCursor = nullptr;
    text = nullptr;
    pointer = nullptr;
    resizeHorizontal = nullptr;
    resizeVertical = nullptr;
  }

  SDL_Cursor* cursor(CursorKind kind) const {
    switch(kind) {
      case CursorKind::Text: return text;
      case CursorKind::Pointer: return pointer;
      case CursorKind::ResizeHorizontal: return resizeHorizontal;
      case CursorKind::ResizeVertical: return resizeVertical;
      case CursorKind::Default:
      default: return defaultCursor;
    }
  }

  void apply(CursorKind kind) {
    if(kind == active) return;
    if(SDL_Cursor* next = cursor(kind)) {
      SDL_SetCursor(next);
      active = kind;
    }
  }
};

struct UiRuntime {
  // ---- the library -------------------------------------------------------
  ui::AppState state;
  // Watches the library tree, so a change made outside micronotes is noticed
  // when it happens rather than the next time the window regains focus. Idle
  // cost is nothing: one inotify descriptor and a thread asleep in `poll`. See
  // `platform::DirectoryWatcher`.
  platform::DirectoryWatcher watcher;

  // ---- the open note -----------------------------------------------------
  editor::MarkdownEditor editor;
  markdown::MarkdownParser parser;
  std::string loadedNoteId;
  // What the shell remembers about it, each with its own forgetting rule. See
  // `app/NoteCaches.h`.
  ComplexParseCache complexParses;
  ImagePathCache imagePaths;
  WikiTargets wikiTargets;
  PageHeaderMemo pageHeader;
  // Which toggles the reader has collapsed, per note. A view preference, so it
  // lives beside the library rather than in the `.md` file.
  ui::FoldState folds;

  // ---- the surfaces that show it -----------------------------------------
  PageView livePage;
  // The reading pane is the same renderer with the caret, the gutter and the
  // toolbar off. A second instance rather than the same one because the two
  // panes lay the note out at different widths -- in split view, at the same
  // time -- and one block cache serving both would be swept on every frame.
  PageView readingPage;
  // The third: the note as a monospaced file. See `app/RawPaneState.h`.
  RawPaneState raw;
  // The two panes above scroll as pixels; the panels own their own
  // accumulators. See `WheelAccumulator`.
  WheelAccumulator liveWheel;
  WheelAccumulator viewerWheel;
  // Where every link the panes drew this frame landed, so a click and the
  // cursor shape can find one without laying a page out again. Cleared at the
  // top of the frame; see `app/PageChrome.h`.
  std::vector<LinkRegion> linkRegions;
  // A heading to scroll to once the note now being opened has been laid out.
  //
  // A link that crosses notes -- `[a section](other.md#a-section)` -- names
  // both a note and a place in it, and the two cannot be honoured at the same
  // moment: opening the note replaces the buffer, but the page is still holding
  // the note you came from until the next frame lays the new one out, and a
  // page's anchor table is built from its own laid-out document. Asking
  // immediately searched the wrong note. See `queueAnchorJump`.
  std::string pendingAnchor;

  // ---- the panels --------------------------------------------------------
  // See `app/SidebarState.h`, `app/PanelState.h`, `app/ChromeState.h`.
  SidebarState sidebar;
  RightPanelState rightPanel;
  // The strip of open notes, as it was last drawn: the draw records it and
  // everything else walks what was drawn. See `app/TabStrip.h`.
  TabStripState tabStrip;
  ChromeState chrome;
  ui::OverlayStack overlays;
  // The Settings card, or About in the same card. See `app/SettingsPane.h`.
  ui::SettingsSurfaceState settings;
  // The mode the last computed layout settled in. Fed back into the next one so
  // the compact breakpoint has hysteresis rather than flipping mid-drag.
  ui::LayoutMode layoutMode = ui::LayoutMode::Regular;
  std::string status;

  // ---- where the reader is -----------------------------------------------
  FocusArea focus = FocusArea::Editor;
  // The five one-line fields; which one is live is a function of `focus`. See
  // `app/TextFields.h` and `app/Fields.h`.
  TextFields fields;
  PointerState pointer;

  // ---- what the reader is doing to the note ------------------------------
  // See `app/EditingState.h`. Three gestures that look alike and behave
  // differently, which is why they are three types.
  DragSelect textSelect;
  DragSelect fieldSelect;
  ClickRun editorClicks;
  BlockSelection blockSelection;
  BlockDrag blockDrag;
  SlashMenu slash;
  // The first "[" of the "[[" that opened the wikilink picker, for the same
  // reason `SlashMenu::start` exists: committing replaces from there.
  std::size_t wikiStart = 0;
  CaretState caret;
  // Whether the next frame should scroll the caret back into view. Set by
  // everything that moves it deliberately and cleared by everything that
  // scrolls on purpose -- a wheel over the page must not be undone by the caret
  // it left behind.
  bool revealEditorCursor = true;

  // ---- when it was last touched ------------------------------------------
  Uint64 lastEdit = 0;
  Uint64 lastAutosaveAttempt = 0;

  // Records that the buffer has changed. The recovery copy is written here
  // rather than at save time, so a crash between keystrokes loses nothing.
  void markEdited() {
    lastEdit = SDL_GetTicks();
    if(!state.hasLibrary() || state.selection().noteId.empty()) return;
    if(!state.saveSelectedNoteRecovery(editor.text())) status = "Recovery save failed";
  }
};

// What the caret is doing, as one value.
//
// Fed to `ui::CaretBlink` once a frame; when it changes, the blink restarts in
// its on-phase, so a keystroke shows a solid caret where the character landed.
//
// A key rather than a `restartBlink()` at every site that could move a caret.
// There are a dozen of those -- every arrow key, every click into text, every
// edit, every focus change, the overlay stack's own field -- the list grows,
// and the one that forgets leaves a caret blinking through a burst of typing.
// The same discipline the sidebar's row memo and the page header use, and for
// the same reason: a key cannot be forgotten, only unequal.
inline std::uint64_t caretStateKey(const UiRuntime& ui) {
  std::uint64_t key = util::kFnvOffset;
  key = util::hashValue(key, ui.focus);
  // The page's caret: where it is, and which revision of the buffer it is in.
  key = util::hashValue(key, ui.editor.revision());
  key = util::hashValue(key, ui.editor.cursor());
  // Whichever single-line field has the keyboard. Its length stands in for its
  // text: this only has to *change* when the field does, and a field cannot
  // change its contents without changing its length or its caret.
  const editor::TextField* field = nullptr;
  switch(ui.focus) {
    case FocusArea::Search: field = &ui.fields.search; break;
    case FocusArea::Find: field = &ui.fields.find; break;
    case FocusArea::TagEditor: field = &ui.fields.tag; break;
    case FocusArea::RenameNote: field = &ui.fields.rename; break;
    case FocusArea::RenameFolder: field = &ui.fields.folderRename; break;
    default: break;
  }
  if(field) {
    key = util::hashValue(key, field->text().size());
    key = util::hashValue(key, field->editor.cursor());
  }
  // And the overlay's own field, which is the one the reader is most likely to
  // be typing into and is not reachable through `ui.focus` at all.
  if(const ui::Overlay* overlay = ui.overlays.top()) {
    key = util::hashValue(key, overlay->value.text().size());
    key = util::hashValue(key, overlay->value.editor.cursor());
    key = util::hashValue(key, overlay->highlighted);
  }
  return key;
}

// How an overlay's answer about the pointer reads as a system cursor.
//
// The shell used to answer `Pointer` for the whole window whenever any overlay
// was open, so the field in a rename box, a tag editor or the command palette
// never showed a text cursor -- and those are the fields a reader is most
// likely to be typing into.
inline CursorKind cursorForOverlay(ui::OverlayCursor over) {
  switch(over) {
    case ui::OverlayCursor::Text: return CursorKind::Text;
    case ui::OverlayCursor::Pointer: return CursorKind::Pointer;
    // Over the panel's own ground, or outside it: a click there does nothing
    // and a click there dismisses, and neither is a control to point at.
    case ui::OverlayCursor::Panel:
    case ui::OverlayCursor::Outside: break;
  }
  return CursorKind::Default;
}

// Settles the caret's blink for this frame, and reports how long until the next
// phase change -- or -1 once it has settled solid and nothing needs waking.
//
// Called from both the frame and the wait, and idempotent within a frame:
// observing the same key twice is a no-op, and both callers want the answer for
// the clock as it is now. Once rather than at each caret's paint, because two
// carets are on screen at once in a split view and they must not blink out of
// step.
inline int settleCaret(UiRuntime& ui) {
  const Uint64 now = SDL_GetTicks();
  ui.caret.blink.observe(caretStateKey(ui), now);
  ui.caret.visible = ui.caret.blink.visible(now);
  return ui.caret.blink.waitMs(now);
}

// Whether the caret's blink has flipped since the last frame painted one.
//
// The run loop repaints on events, on a window action, on a watched change and
// on an autosave -- and the caret is behind none of those. So a blink wake
// arrived, found nothing to do, and counted a skipped repaint: the deadline was
// honoured and the frame it existed for was never drawn.
inline bool caretPhaseChanged(UiRuntime& ui) {
  (void)settleCaret(ui);
  return ui.caret.visible != ui.caret.painted;
}

// Every caller goes through here so that the rects a frame is painted with, the
// rects it is hit-tested against and the rects the tests assert on are the same
// rects. `ui.layoutMode` is both an input and an output: feeding the last mode
// back in is what gives the compact breakpoint its hysteresis.
inline ShellLayout shellLayout(UiRuntime& ui, int width, int height) {
  auto inputs = ui.state.workspace().layoutInputs(
    static_cast<float>(width), static_cast<float>(height), ui.layoutMode);
  // One tab is still a tab: hiding the strip until a second opens would make
  // the page jump down the moment it did.
  inputs.tabStripVisible = !ui.state.workspace().tabs.empty();
  const ShellLayout layout = ui::computeShellLayout(inputs);
  ui.layoutMode = layout.mode;
  return layout;
}

}
