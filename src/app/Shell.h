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
#include "ui/ShellLayout.h"
#include "ui/TextUtil.h"
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

// Everything the running shell knows: the open note, what has focus, what is
// being dragged, and the hit rects the last frame recorded.
//
// This lived in Application.cpp's anonymous namespace, which meant no other
// translation unit could name it -- and so nothing could be moved out of that
// file no matter how large it grew. Hoisting it here is what makes the rest of
// the decomposition possible; it is not, by itself, a design.
namespace micronotes::app {

using micronotes::ui::Rect;
using micronotes::ui::ShellLayout;
using micronotes::ui::TextRenderer;

enum class UiAction {
  Refresh,
  NewNote,
  RenameNote,
  DeleteNote,
  Save,
  Tags,
  PaneEditor,
  PaneViewer,
  PaneSplit
};

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

enum class ScrollDragTarget {
  Live,
  None,
  Editor,
  Viewer,
  // The sidebar's own scrollbar. It had none: the old bar was a 3px hairline
  // with a 5px thumb, which nobody would try to grab, so nothing answered when
  // they did. A 10px track with a thumb filling it reads as a handle, and a
  // handle that does not move when pulled is worse than no handle.
  Sidebar
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

// What the raw pane's soft wrap was computed from: the buffer, the column, and
// the face it was measured in. The pane shows the file as bytes, so its line
// breaks are the file's rather than the layout's -- which is why it is the one
// surface with a wrap of its own to memoise.
// The question the sidebar's result list answers, and the library it answered
// it against.
struct SearchKey {
  std::string query;
  library::SearchScope scope = library::SearchScope::All;
  std::uint64_t libraryRevision = 0;
  bool operator==(const SearchKey&) const = default;
};

struct RawRowsKey {
  std::uint64_t revision = 0;
  int wrapWidth = -1;
  ui::TextSize textSize = ui::TextSize::Medium;
  bool operator==(const RawRowsKey&) const = default;
};

struct LinkRegion {
  Rect rect;
  std::string target;
  // See PageLink::wiki: a link to a note is followed differently from a link
  // to a file, and the two are indistinguishable once they are just strings.
  bool wiki = false;
};

struct ButtonRegion {
  Rect rect;
  UiAction action = UiAction::Refresh;
};

// One drawn line of the sidebar. The tree and the tag filter share a single
// scrolling list, so there is one geometry to draw, one to hit-test, and one
// thing to scroll.
struct SidebarRow {
  enum class Kind {
    Tree,
    // A band across the panel heading the rows under it. Two sorts: one that
    // names a `SidebarSection` and can be shut, and one that is a caption on
    // what the list is currently showing -- the result count, the tag being
    // filtered by -- which has nothing to shut.
    SectionLabel,
    Tag,
    // A note that matched the query, with the lines that matched under it.
    // While a search is running these replace the tree rather than appearing
    // beside them: the sidebar answers one question at a time.
    SearchResult
  };

  Kind kind = Kind::Tree;
  Rect rect;
  std::string label;   // section labels only
  // Section labels only: which band this is, when it is one the reader can
  // shut. Absent on a caption, which is what tells the draw not to give it a
  // chevron and the click not to look for one.
  std::optional<ui::SidebarSection> section;
  bool collapsed = false;
  // Drawn right-aligned in a band: how many rows are under it. It is worth
  // most on a band that is *shut*, which is the one case where the rows cannot
  // be counted by looking.
  std::string trailing;
  // A folder row with something inside it, and a band that can be shut, both
  // get one -- they are the same control doing the same job. An empty rect
  // means the whole row acts rather than expanding.
  Rect disclosure;
  ui::TreeRow tree;
  std::string tag;
  // Search results only.
  std::string noteId;
  std::string title;
  // The matching lines alone, without the context either side. A sidebar-width
  // column has no room for three lines per hit, and showing them would triple
  // every row's height for text nobody can read at that width.
  //
  // Already trimmed to the column, and carrying where the match sits inside
  // what is left, so the draw marks the span and does not have to measure the
  // line again on every frame. `length` is zero when there is nothing to mark
  // -- a note whose title matched but whose text did not.
  //
  // Filled on the first frame the row is *drawn*, not when the list is built.
  // Trimming one matching line to the column measures the whole line and then
  // searches, with a measurement per probe, and every probe is a string nothing
  // has measured before -- so the measure cache cannot help and each is a real
  // shaping pass. A 200-result query has 600 of them, and building all of them
  // up front made the first frame of a query a 150-500 ms freeze, per keystroke
  // of the query.
  // The row list stays O(results), because the heights have to add up to a
  // scrollbar; the trimming is O(viewport).
  std::vector<ui::SnippetWindow> matchLines;
  // Which result this row lists, and how many lines it will show once they are
  // trimmed. The count is known without measuring anything, which is what lets
  // the row take its final height before its text exists.
  std::size_t resultIndex = 0;
  std::size_t matchLineCount = 0;
  bool matchLinesBuilt = false;
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

// What the drawn window controls ask the run loop to do.
enum class WindowAction {
  None,
  Minimize,
  ToggleMaximize,
  Close
};

struct UiRuntime {
  ui::AppState state;
  // Watches the library tree, so a change made outside micronotes is noticed
  // when it happens rather than the next time the window regains focus. Idle
  // cost is nothing: one inotify descriptor and a thread asleep in `poll`. See
  // `platform::DirectoryWatcher`.
  platform::DirectoryWatcher watcher;
  editor::MarkdownEditor editor;
  markdown::MarkdownParser parser;
  PageView livePage;
  // The reading pane is the same renderer with the caret, the gutter and the
  // toolbar off. A second instance rather than the same one because the two
  // panes lay the note out at different widths -- in split view, at the same
  // time -- and one block cache serving both would be swept on every frame.
  PageView readingPage;
  // What the shell remembers about the note on the page. Each one carries its
  // own forgetting rule; see `app/NoteCaches.h`.
  ComplexParseCache complexParses;
  ImagePathCache imagePaths;
  // The raw pane's soft-wrapped rows. See `editorRows`.
  ui::Memo<std::vector<editor::SoftWrapRow>, RawRowsKey> rawRows;
  FocusArea focus = FocusArea::Editor;
  std::string loadedNoteId;
  editor::TextField search;
  library::SearchScope searchScope = library::SearchScope::All;
  editor::TextField find;
  editor::TextField tag;
  editor::TextField rename;
  editor::TextField folderRename;
  std::string status;
  std::vector<LinkRegion> linkRegions;
  std::vector<ButtonRegion> buttonRegions;
  int noteCursor = 0;
  int folderCursor = 0;
  int editorScroll = 0;
  // Rows the raw editor last had room for, so PageUp/PageDown match the view.
  int editorVisibleRows = 20;
  // One per scrolling surface. See WheelAccumulator.
  WheelAccumulator editorWheel;
  WheelAccumulator viewerWheel;
  WheelAccumulator liveWheel;
  WheelAccumulator sidebarWheel;
  Uint64 lastRefresh = 0;
  float mouseX = -1;
  float mouseY = -1;
  ui::OverlayStack overlays;
  WikiTargets wikiTargets;
  // The first "[" of the "[[" that opened the wikilink picker.
  std::size_t wikiStart = 0;
  // A heading to scroll to once the note now being opened has been laid out.
  //
  // A link that crosses notes -- `[a section](other.md#a-section)` -- names
  // both a note and a place in it, and the two cannot be honoured at the same
  // moment: opening the note replaces the buffer, but the page is still holding
  // the note you came from until the next frame lays the new one out, and a
  // page's anchor table is built from its own laid-out document. Asking
  // immediately searched the wrong note. See `queueAnchorJump`.
  std::string pendingAnchor;
  // The open note's front matter, drawn above its first block. Producing it
  // means reading the file, so it is memoised on the note and the library's
  // revision rather than rebuilt per frame.
  PageHeaderMemo pageHeader;

  // The right-hand panel, and its own memos. See `app/PanelState.h`.
  RightPanelState rightPanel;

  // What the pointer is resting on. Cleared at the start of a frame and set by
  // whichever surface the pointer is over, so exactly one is ever showing.
  ui::HoverTooltip tooltip;

  // Offers a tooltip for `control` when the pointer is inside it. The last
  // caller wins, which is the innermost surface: a tooltip on a tab's close
  // button should beat the one on the tab.
  bool hovered(Rect control) const {
    return ui::contains(control, mouseX, mouseY);
  }

  void offerTooltip(Rect control, std::string text) {
    if(!ui::contains(control, mouseX, mouseY)) return;
    tooltip = {std::move(text), control};
  }
  // The mode the last computed layout settled in. Fed back into the next one so
  // the compact breakpoint has hysteresis rather than flipping mid-drag.
  ui::LayoutMode layoutMode = ui::LayoutMode::Regular;
  // Which toggles each note has collapsed. A view preference, so it lives
  // beside the library rather than in the `.md` file.
  ui::FoldState folds;
  // Which sidebar folders are open. Also a view preference, and also kept out
  // of the library: a disclosure triangle must not touch a file.
  ui::TreeModel tree;
  std::vector<SidebarRow> sidebarRows;
  // What `sidebarRows` was built from. The list is a pure function of these,
  // and it used to be rebuilt on every frame -- a relativised path, a map key
  // and a row object per note in the library, to draw the three dozen rows the
  // panel is tall enough to show. Only the scroll and the panel origin move on
  // a typical frame, and both are an offset applied to a list already built.
  struct SidebarRowsKey {
    bool valid = false;
    std::uint64_t stateRevision = 0;
    std::uint64_t treeRevision = 0;
    std::string search;
    library::SearchScope searchScope = library::SearchScope::All;
    std::string tag;
    std::vector<std::string> favorites;
    std::vector<std::string> recents;
    // Which bands are shut. Every row below a shut band moves, so this is
    // geometry like the panel's width is -- and a key rather than a flag, for
    // the reason the comment above gives: a flag has to be raised at every site
    // that shuts a band, and the one that forgets leaves the panel hit-testing
    // rows it is no longer drawing.
    std::array<bool, 4> collapsedSections {};
    float width = 0.0f;
    float height = 0.0f;
    // The rhythm the rows were laid out at. Every height in the list derives
    // from these two, so a change of text size has to rebuild rather than shift.
    float rowHeight = 0.0f;
    float snippetHeight = 0.0f;
    // Where the rows were placed, so a pure scroll shifts them instead of
    // rebuilding them.
    float originX = 0.0f;
    float originY = 0.0f;
    int scroll = 0;
    float contentHeight = 0.0f;
  };
  SidebarRowsKey sidebarRowsKey;
  // The results the sidebar is currently listing. Held rather than re-queried
  // because the row list is rebuilt on every frame -- including the ones a
  // hover causes -- and each query is a hit on SQLite. Keyed on the library
  // revision as well as the query, so an edit that changes what matches is not
  // served a stale answer.
  ui::Memo<std::vector<library::SearchResult>, SearchKey> searchResults;
  // Last frame's sidebar rect, so keyboard navigation can scroll a row into
  // view without recomputing the whole window layout.
  Rect sidebarRect;
  int sidebarScroll = 0;
  int sidebarMaxScroll = 0;
  bool creatingFolder = false;
  bool draggingNote = false;
  std::string draggingNoteId;
  // Dragging a folder onto another re-parents it; the row under the pointer is
  // the drop target, and is highlighted rather than merely guessed at.
  bool draggingFolder = false;
  std::filesystem::path draggingFolderPath;
  std::optional<std::size_t> sidebarDropRow;
  ScrollDragTarget scrollDragTarget = ScrollDragTarget::None;
  float scrollDragOffsetY = 0.0f;
  Rect searchScopeToggle;
  // Which menu on the bar is open, and which of its rows the keyboard is on.
  // The pointer owns the highlight while it is inside the popup, so a walk with
  // the arrows followed by a mouse move does not leave two rows lit.
  ui::MenuId openMenu = ui::MenuId::None;
  std::size_t menuHighlight = 0;
  // Last frame's menu bar, so the popup -- which is drawn after every panel, on
  // top of them -- can find the item it hangs off without being handed the
  // whole layout.
  Rect menuBarRect;
  // The bar's targets, recorded as they are drawn: the run the menus occupy,
  // and the overflow chevron. Recorded rather than recomputed because the one
  // caller that needs them is the borderless window's hit test, which runs on
  // the platform's callback with no renderer to measure a label with -- and
  // everything in the bar that is *not* one of these is the strip the window is
  // dragged by. The window buttons below are recorded for the same reason.
  Rect menuItemsBand;
  Rect menuChevron;
  // The breadcrumb trail over the page, recorded as it is drawn: a crumb is a
  // folder to jump to, and the star at the end pins the note.
  std::vector<std::pair<Rect, std::filesystem::path>> crumbs;
  Rect favoriteButton;
  // What a click on a window control asked for, held until the frame is over.
  // The buttons are drawn and hit-tested in shell code that has no business
  // knowing about SDL_Window; the run loop, which owns the window, acts on it.
  WindowAction pendingWindowAction = WindowAction::None;
  // Whether this window draws its own controls instead of wearing the
  // compositor's. Cleared when the platform refuses a hit test, because a
  // borderless window nobody can move is worse than a decorated one.
  bool customChrome = true;
  // Minimise, maximise, close -- in that order, recorded as they are drawn so
  // the hit test can exempt them from the draggable strip around them.
  std::array<Rect, 3> windowButtons {};
  // Whether the window is maximised, so the middle button can draw the restore
  // glyph instead. Tracked from window events rather than queried in the draw,
  // which would ask the display server a question every frame.
  bool windowMaximized = false;
  bool resizingSidebar = false;
  // Set while a drag inside a single-line field is extending its selection.
  bool selectingFieldText = false;
  std::size_t fieldSelectionAnchor = 0;

  bool selectingEditorText = false;
  std::size_t editorSelectionAnchor = 0;
  Uint64 lastEditorClick = 0;
  int editorClickCount = 0;
  // Block multi-select, held as source offsets rather than block indices so an
  // edit underneath it cannot silently re-point it at a different block.
  bool blockSelectActive = false;
  std::size_t blockSelectAnchor = 0;
  std::size_t blockSelectFocus = 0;
  bool draggingBlock = false;
  std::size_t dragBlockAnchor = 0;
  std::size_t dragBlockFocus = 0;
  std::optional<std::size_t> blockDropOffset;
  // Where the "/" that opened the slash menu sits, so committing can erase it.
  std::size_t slashStart = 0;
  // The gutter's insert button opens the same menu, but to add a block after
  // this one rather than to rewrite the block the caret is in.
  bool slashInserts = false;
  std::size_t slashAfterBlock = 0;
  bool revealEditorCursor = true;
  // Caret rectangle in window coordinates, published to SDL each frame so the
  // IME can position its candidate window. Zero-sized until the editor draws.
  SDL_Rect caretRect {0, 0, 0, 0};
  bool caretReported = false;
  // Whether the caret is painted this instant, and what it was last doing.
  //
  // Every caret in the shell was drawn solid, which in a mono chrome face reads
  // as a pipe character rather than as an insertion point -- so the one thing
  // whose job is to say "your typing goes here" said it in the same voice as
  // the text around it. See `ui::CaretBlink`; `caretStateKey` is what it is fed.
  ui::CaretBlink caretBlink;
  bool caretVisible = true;
  // What the last painted frame actually drew. The blink is the only thing in
  // the shell that changes with no event behind it, so it is the only thing
  // whose "is a repaint due" question cannot be answered by the event queue --
  // see `caretPhaseChanged`.
  bool caretPainted = true;
  Uint64 lastEdit = 0;
  Uint64 lastAutosaveAttempt = 0;

  // Leaving block-selection mode. On the runtime rather than a free function,
  // because it is one field and two translation units would otherwise have to
  // agree about which of them owns setting it.
  void clearBlockSelection() {
    blockSelectActive = false;
  }

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
    case FocusArea::Search: field = &ui.search; break;
    case FocusArea::Find: field = &ui.find; break;
    case FocusArea::TagEditor: field = &ui.tag; break;
    case FocusArea::RenameNote: field = &ui.rename; break;
    case FocusArea::RenameFolder: field = &ui.folderRename; break;
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
  ui.caretBlink.observe(caretStateKey(ui), now);
  ui.caretVisible = ui.caretBlink.visible(now);
  return ui.caretBlink.waitMs(now);
}

// Whether the caret's blink has flipped since the last frame painted one.
//
// The run loop repaints on events, on a window action, on a watched change and
// on an autosave -- and the caret is behind none of those. So a blink wake
// arrived, found nothing to do, and counted a skipped repaint: the deadline was
// honoured and the frame it existed for was never drawn.
inline bool caretPhaseChanged(UiRuntime& ui) {
  (void)settleCaret(ui);
  return ui.caretVisible != ui.caretPainted;
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
