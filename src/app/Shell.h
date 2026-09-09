#pragma once

#include "CoreAliases.h"

#include "app/PageView.h"
#include "core/editor/MarkdownEditor.h"
#include "core/editor/TextField.h"
#include "core/markdown/MarkdownParser.h"
#include "core/platform/DirectoryWatcher.h"
#include "app/NoteCaches.h"
#include "app/PanelState.h"
#include "app/PointerState.h"
#include "app/ChromeState.h"
#include "app/EditingState.h"
#include "app/Fields.h"
#include "app/FindState.h"
#include "app/Focus.h"
#include "app/LinkRegion.h"
#include "app/RawPaneState.h"
#include "app/ResizePacing.h"
#include "app/TextFields.h"
#include "app/SidebarState.h"
#include "app/StatusBar.h"
#include "app/StatusLine.h"
#include "app/TabStrip.h"
#include "app/Wheel.h"
#include "doc/BlockScan.h"
#include "library/Library.h"
#include "ui/AppState.h"
#include "ui/TextRenderer.h"
#include "ui/NoteProperties.h"
#include "ui/Outline.h"
#include "ui/FoldState.h"
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

struct UiRuntime {
  // ---- the library -------------------------------------------------------
  ui::AppState state;
  // Watches the library tree, so a change made outside micronotes is noticed
  // when it happens rather than the next time the window regains focus. Idle
  // cost is nothing: one inotify descriptor and a thread asleep in `poll`. See
  // `platform::DirectoryWatcher`.
  platform::DirectoryWatcher watcher;

  // Whether the frame waits for the display. It does, except while the window
  // is being dragged by its edge; see `app/ResizePacing.h`.
  ResizePacing pacing;

  // How a companion file is opened: handed to the desktop's default handler,
  // unless a test has put something else here. Empty means the desktop. A seam
  // rather than a direct call so the rule -- a click launches, a cursor passing
  // over never does -- can be checked without spawning `xdg-open` from a test.
  std::function<bool(const std::filesystem::path&)> launcher;

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
  // What the shell has just been told to say, with the moment it was told. The
  // status bar shows it for a few seconds and then goes back to reporting what
  // is true; see `app/StatusLine.h` for why it is a type rather than a string.
  StatusLine status;
  // The two memos the bar's readouts stand on. See `app/StatusBar.h`.
  StatusBarState statusBar;

  // ---- where the reader is -----------------------------------------------
  FocusArea focus = FocusArea::Editor;
  // The five one-line fields; which one is live is a function of `focus`. See
  // `app/TextFields.h` and `app/Fields.h`.
  TextFields fields;
  // The find bar over the page: what it found and which match you are on. See
  // `app/FindState.h`; the needle itself is `fields.find`.
  FindState find;
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
  // The same for the reading pane, which needs its own flag because it has no
  // caret: nothing about it moves when the buffer's cursor does, so the pane it
  // shares a window with in split view consumes `revealEditorCursor` first and
  // the reading side never hears about it. Set only by things that move the
  // *selection* on purpose -- stepping through find matches is the one so far.
  bool revealViewerSelection = false;

  // ---- when it was last touched ------------------------------------------
  Uint64 lastEdit = 0;
  Uint64 lastAutosaveAttempt = 0;

  // What the note area is showing.
  //
  // Here rather than spelled `state.workspace().paneMode()` at each of the
  // twenty-two sites that ask, which is three objects deep for the question the
  // shell asks most often about the thing it is drawing -- and every one of
  // those sites was reaching through a *mutable* accessor to read a value.
  ui::PaneMode paneMode() const { return state.workspace().paneMode(); }

  // Records that the buffer has changed. The recovery copy is written here
  // rather than at save time, so a crash between keystrokes loses nothing.
  void markEdited() {
    lastEdit = SDL_GetTicks();
    if(!state.catalog().isOpen() || state.selection().noteId.empty()) return;
    if(!state.openNote().saveRecovery(editor.text())) status = "Recovery save failed";
  }
};

}
