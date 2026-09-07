#pragma once

#include "CoreAliases.h"

#include "app/PageView.h"
#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"
#include "core/editor/TextField.h"
#include "core/markdown/MarkdownParser.h"
#include "core/platform/DirectoryWatcher.h"
#include "doc/BlockScan.h"
#include "library/Library.h"
#include "ui/AppState.h"
#include "ui/Draw.h"
#include "ui/NoteProperties.h"
#include "ui/Outline.h"
#include "ui/FoldState.h"
#include "ui/Actions.h"
#include "ui/Menus.h"
#include "ui/Overlay.h"
#include "ui/Rect.h"
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
  // Only folder rows with something inside them get one; an empty rect means
  // the whole row selects rather than expands.
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

constexpr int kEditorPageLines = 20;

// How far one notch of the wheel moves each surface. In lines for the raw
// editor, which scrolls by row; in pixels for everything else, which scrolls by
// distance. Together here rather than beside their own call sites, because the
// only way to tell whether two surfaces scroll at the same rate is to read the
// numbers next to each other.
constexpr float kEditorScrollLinesPerNotch = 3.0f;
constexpr float kViewerScrollPixelsPerNotch = 42.0f;
constexpr float kLiveScrollPixelsPerNotch = 42.0f;
constexpr float kSidebarScrollPixelsPerNotch = 42.0f;
constexpr float kRightPanelScrollPixelsPerNotch = 42.0f;

// A wheel gesture accumulated to whole units.
//
// SDL reports `wheel.y` in notches for a discrete wheel and in fractions of a
// notch for a precise one: a trackpad delivers a stream of deltas well below
// 1.0. Truncating each event to an int discards them, so a slow gesture scrolls
// nothing at all and a fast one moves in visible jumps. Carrying the remainder
// across events makes the movement track the finger.
//
// Every scrolling surface owns one. It used to be two floats on the runtime
// with the arithmetic written out at each site, which is why the sidebar and the
// live page -- the two surfaces added after it -- did not get it.
struct WheelAccumulator {
  float remainder = 0.0f;

  // Whole units to scroll by, positive downwards. `notches` is SDL's sign
  // convention, where a positive value means the content moves down.
  int take(float notches, float unitsPerNotch) {
    remainder += -notches * unitsPerNotch;
    const float whole = std::trunc(remainder);
    remainder -= whole;
    return static_cast<int>(whole);
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
  // md4c documents for the blocks the live surface hands off, keyed by source.
  // Keyed by the block's own source text, and looked up through a view --
  // `std::less<>` rather than the default, so finding a parse does not first
  // allocate a copy of the bytes to look it up by.
  std::map<std::string, markdown::Document, std::less<>> complexCache;
  // How many distinct `Complex` blocks the last sweep found in the note. The
  // cache is allowed to run this far past it before the next sweep, which is
  // what makes "is a sweep due" one comparison rather than a walk of the note.
  std::size_t complexCacheLive = 0;
  // Image targets resolved to files on disk, or to nothing when the target
  // names no drawable file. Memoised because resolving one canonicalises both
  // the library root and the candidate -- a `stat` per path component of each,
  // twice -- and the layout asks per picture per relaid block: a note of 200
  // pictures spent about 3,200 syscalls being laid out, which was 47 ms of its
  // first frame. Dropped when the library root moves under it.
  std::map<std::string, std::filesystem::path, std::less<>> imagePaths;
  std::filesystem::path imagePathRoot;
  std::string cachedEditorRowsSource;
  int cachedEditorRowsWidth = -1;
  std::vector<editor::SoftWrapRow> cachedEditorRows;
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
  WheelAccumulator rightPanelWheel;
  Uint64 lastRefresh = 0;
  float mouseX = -1;
  float mouseY = -1;
  ui::OverlayStack overlays;
  // Every note in the library, for resolving wikilinks. Invalidated rather than
  // rebuilt on every layout: a note with fifty links would otherwise list the
  // whole library fifty times per keystroke.
  std::vector<library::NoteListItem> wikiNotes;
  bool wikiNotesValid = false;
  // Moves with every invalidation of the list above, so the layout can be told
  // that what a `[[target]]` resolves to may have changed. See
  // `invalidateWikiNotes`, which is the only thing that should touch either.
  std::uint64_t wikiNotesRevision = 1;
  // The first "[" of the "[[" that opened the wikilink picker.
  std::size_t wikiStart = 0;
  // Where the backlinks panel drew each row last frame, so a click can find the
  // note it named without laying the list out a second time.
  struct BacklinkRow {
    Rect rect;
    std::string noteId;
  };
  std::vector<BacklinkRow> backlinkRows;
  // The same, for the right panel's tag rows. A tag there filters the library
  // exactly as a tag in the sidebar does, so a click has to be able to find
  // which one it landed on.
  struct TagRow {
    Rect rect;
    std::string tag;
  };
  std::vector<TagRow> tagRows;
  // The open note's front matter, drawn above its first block.
  //
  // Producing it means reading the file, so it is cached against the note and
  // the library's revision rather than rebuilt per frame -- the header is drawn
  // every frame and a note is read from disk when it is opened, renamed,
  // retagged or re-iconed, all of which move the revision.
  std::vector<ui::NoteProperty> headerProperties;
  std::string headerNoteId;
  std::uint64_t headerRevision = 0;
  bool headerValid = false;

  // What the right panel is showing, cached against the inputs that decide it.
  //
  // All three of its views were rebuilt on every frame, and each was expensive
  // in a different way. Measured over a real session on a 400-note library with
  // a 200 KB note open, per frame: the outline scanned the whole note for its
  // headings, 0.30 ms; the backlinks ran a SQLite query, 0.25 ms; and the tags
  // *read the note back off disk*, 0.53 ms. The same frame drew the note itself
  // in 0.16 ms -- so an idle frame spent two to three times as long on the panel
  // beside the note as on the note.
  //
  // Two keys, because the three views do not depend on the same things. The
  // outline is a function of the buffer, so it turns on the editor's revision
  // and has to move while the user types. Backlinks and tags come from the
  // library -- the index and the note's front matter -- so they turn on the note
  // id and the library's revision, and typing must *not* move them.
  //
  // Keyed rather than invalidated by a flag, for the reason the page header
  // above gives: a flag has to be raised at every mutation site and the one that
  // forgets leaves the panel describing a note that has moved on.
  struct RightPanelMemo {
    std::vector<ui::OutlineEntry> outline;
    std::uint64_t outlineRevision = 0;
    bool outlineValid = false;

    std::vector<library::Backlink> backlinks;
    std::vector<std::string> tags;
    std::string noteId;
    std::uint64_t libraryRevision = 0;
    bool libraryValid = false;

    // Which view of which note the panel's scroll offset belongs to. Switching
    // either starts the list at the top; see `resetScrollOnChange`. Two fields
    // rather than one joined key, because this is compared on every frame and a
    // joined key would build a string on every one of them to find out that
    // nothing had moved.
    ui::RightPanelView scrollView = ui::RightPanelView::Outline;
    std::string scrollNoteId;
    bool scrollKeyValid = false;
  };
  RightPanelMemo rightPanel;

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
  std::string searchCacheQuery;
  library::SearchScope searchCacheScope = library::SearchScope::All;
  std::uint64_t searchCacheRevision = 0;
  bool searchCacheValid = false;
  std::vector<library::SearchResult> searchCache;
  // Last frame's sidebar rect, so keyboard navigation can scroll a row into
  // view without recomputing the whole window layout.
  Rect sidebarRect;
  int sidebarScroll = 0;
  int sidebarMaxScroll = 0;
  // The right-hand panel scrolls like every other list in the shell. It used to
  // be the one that did not: a note with more headings than the panel was tall
  // simply stopped listing them, with no scrollbar to say so and a wheel over it
  // scrolling the note behind instead.
  int rightPanelScroll = 0;
  int rightPanelMaxScroll = 0;
  // Last frame's right-panel rect, so a wheel can be clamped to the same
  // maximum the draw computed without laying the panel out a second time.
  Rect rightPanelRect;
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
