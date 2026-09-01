#pragma once

#include "CoreAliases.h"

#include "app/PageView.h"
#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"
#include "core/editor/TextField.h"
#include "core/markdown/MarkdownParser.h"
#include "doc/BlockScan.h"
#include "library/Library.h"
#include "ui/AppState.h"
#include "ui/Draw.h"
#include "ui/FoldState.h"
#include "ui/Actions.h"
#include "ui/Overlay.h"
#include "ui/Rect.h"
#include "ui/ShellLayout.h"
#include "ui/TreeModel.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
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
  Notes,
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
  Viewer
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
    case FocusArea::Notes: return "Notes";
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
    Tag
  };

  Kind kind = Kind::Tree;
  Rect rect;
  std::string label;   // section labels only
  // Only folder rows with something inside them get one; an empty rect means
  // the whole row selects rather than expands.
  Rect disclosure;
  ui::TreeRow tree;
  std::string tag;
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

constexpr float kEditorScrollLinesPerNotch = 3.0f;
constexpr float kViewerScrollPixelsPerNotch = 42.0f;

struct UiRuntime {
  ui::AppState state;
  editor::MarkdownEditor editor;
  markdown::MarkdownParser parser;
  PageView livePage;
  // md4c documents for the blocks the live surface hands off, keyed by source.
  std::map<std::string, markdown::Document> complexCache;
  std::string cachedMarkdownSource;
  std::optional<markdown::Document> cachedMarkdownDocument;
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
  std::map<std::string, int> viewerAnchors;
  std::vector<ButtonRegion> buttonRegions;
  int noteCursor = 0;
  int folderCursor = 0;
  int editorScroll = 0;
  // Rows the raw editor last had room for, so PageUp/PageDown match the view.
  int editorVisibleRows = 20;
  int viewerScroll = 0;
  // Fractional remainder of a scroll gesture, in lines (editor) and pixels
  // (viewer). A high-resolution wheel or a trackpad delivers deltas well below
  // 1.0 per event; truncating each one to an int discarded them entirely, so
  // slow gestures scrolled nothing at all and fast ones moved in visible jumps.
  // Carrying the remainder across events makes the movement track the finger.
  float editorScrollRemainder = 0.0f;
  float viewerScrollRemainder = 0.0f;
  Uint64 lastRefresh = 0;
  float mouseX = -1;
  float mouseY = -1;
  ui::OverlayStack overlays;
  // Every note in the library, for resolving wikilinks. Invalidated rather than
  // rebuilt on every layout: a note with fifty links would otherwise list the
  // whole library fifty times per keystroke.
  std::vector<library::NoteListItem> wikiNotes;
  bool wikiNotesValid = false;
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
  // The breadcrumb trail above the page, recorded as it is drawn: a crumb is a
  // folder to jump to, and the star at the end pins the note.
  std::vector<std::pair<Rect, std::filesystem::path>> crumbs;
  Rect favoriteButton;
  bool resizingSidebar = false;
  bool resizingNotes = false;
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
};
}
