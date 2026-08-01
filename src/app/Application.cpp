#include "CoreAliases.h"
#include "app/Application.h"

#include "app/PageView.h"
#include "core/attachments/AttachmentService.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "doc/Fold.h"
#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"
#include "core/markdown/MarkdownParser.h"
#include "core/platform/DurableFile.h"
#include "core/perf/Perf.h"
#include "core/platform/PathUtils.h"
#include "ui/AppState.h"
#include "ui/Draw.h"
#include "ui/FoldState.h"
#include "ui/Overlay.h"
#include "ui/Settings.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/TreeModel.h"

#include <SDL3/SDL.h>
#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#if MICRONOTES_HAS_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <sys/types.h>
#include <unistd.h>

namespace micronotes::app {
namespace {

using micronotes::ui::ClipGuard;
using micronotes::ui::ImageCache;
using micronotes::ui::Rect;
using micronotes::ui::TextRenderer;
using micronotes::ui::clipRect;
using micronotes::ui::contains;
using micronotes::ui::drawSelection;
using micronotes::ui::drawSurface;
using micronotes::ui::ellipsizeToWidth;
using micronotes::ui::fill;
using micronotes::ui::hLine;
using micronotes::ui::sdlRect;
using micronotes::ui::stroke;
using micronotes::ui::theme;

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

static const char* focusName(FocusArea focus) {
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

static bool inputDebugEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("MICRONOTES_DEBUG_INPUT");
    return value && *value && std::string_view(value) != "0";
  }();
  return enabled;
}

struct LinkRegion {
  Rect rect;
  std::string target;
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

struct AppLayout {
  Rect sidebar;
  Rect notes;
  // The breadcrumb strip above the page. Reserved whether or not a note is open
  // so that every hit test against `content` agrees with what was drawn.
  Rect crumbs;
  Rect content;
  Rect status;
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

static std::string trimTitle(std::string_view text) {
  std::istringstream lines {std::string(text)};
  std::string line;
  while(std::getline(lines, line)) {
    while(!line.empty() && (line.front() == '#' || std::isspace(static_cast<unsigned char>(line.front())))) {
      line.erase(line.begin());
    }
    if(!line.empty()) return line.substr(0, 60);
  }
  return "Untitled";
}

static std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string current;
  for(const char c : text) {
    if(c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  lines.push_back(current);
  return lines;
}

static std::string ellipsize(std::string text, std::size_t limit) {
  if(text.size() <= limit) return text;
  if(limit <= 3) return text.substr(0, limit);
  return text.substr(0, limit - 3) + "...";
}

constexpr float kBreadcrumbHeight = 30.0f;

static AppLayout computeLayout(const ui::ShellModel& shell, int width, int height) {
  const float statusH = 28.0f;
  const float usableW = static_cast<float>(std::max(width, 760));
  float sideW = static_cast<float>(shell.sidebarWidth);
  float notesW = static_cast<float>(shell.noteListWidth);
  if(usableW < 1000.0f) {
    sideW = 190.0f;
    notesW = 240.0f;
  }
  sideW = std::clamp(sideW, 170.0f, std::max(170.0f, usableW * 0.28f));
  notesW = std::clamp(notesW, 220.0f, std::max(220.0f, usableW * 0.34f));
  const float contentMin = 320.0f;
  if(sideW + notesW + contentMin > usableW) {
    notesW = std::max(190.0f, usableW - sideW - contentMin);
  }
  if(sideW + notesW + contentMin > usableW) {
    sideW = std::max(150.0f, usableW - notesW - contentMin);
  }

  const float contentW = static_cast<float>(width) - sideW - notesW;
  const float paneH = static_cast<float>(height) - statusH;
  return {
    {0, 0, sideW, paneH},
    {sideW, 0, notesW, paneH},
    {sideW + notesW, 0, contentW, kBreadcrumbHeight},
    {sideW + notesW, kBreadcrumbHeight, contentW, paneH - kBreadcrumbHeight},
    {0, paneH, static_cast<float>(width), statusH},
  };
}

static bool isRemoteTarget(std::string_view target) {
  return target.starts_with("http://") || target.starts_with("https://");
}

static std::string fileNameForMime(std::string_view mime) {
  if(mime == "image/png") return "clipboard.png";
  if(mime == "image/jpeg" || mime == "image/jpg") return "clipboard.jpg";
  if(mime == "image/bmp") return "clipboard.bmp";
  if(mime == "image/webp") return "clipboard.webp";
  return "clipboard-image";
}

static std::vector<std::string> splitTags(std::string_view value) {
  std::vector<std::string> tags;
  std::set<std::string> seen;
  std::istringstream in {std::string(value)};
  std::string tag;
  while(in >> tag) {
    if(!tag.empty() && tag.front() == '#') tag.erase(tag.begin());
    if(tag.empty() || seen.contains(tag)) continue;
    seen.insert(tag);
    tags.push_back(tag);
  }
  return tags;
}

static std::string joinTags(const std::vector<std::string>& tags) {
  std::string out;
  for(const auto& tag : tags) {
    if(!out.empty()) out += " ";
    out += tag;
  }
  return out;
}

static bool spawnDetached(const std::vector<std::string>& command) {
  if(command.empty()) return false;
  const pid_t pid = fork();
  if(pid < 0) return false;
  if(pid == 0) {
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for(const auto& part : command) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  return true;
}

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
  std::string searchDraft;
  library::SearchScope searchScope = library::SearchScope::All;
  std::string findDraft;
  std::string tagDraft;
  std::string renameDraft;
  std::string folderRenameDraft;
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
  Uint64 lastRefresh = 0;
  float mouseX = -1;
  float mouseY = -1;
  ui::OverlayStack overlays;
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
  bool selectingEditorText = false;
  std::size_t editorSelectionAnchor = 0;
  Uint64 lastEditorClick = 0;
  int editorClickCount = 0;
  bool inputAllSelected = false;
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
  Uint64 lastEdit = 0;
  Uint64 lastAutosaveAttempt = 0;
};

static bool saveCurrent(UiRuntime& ui, bool quiet = false);
static void openDeleteNoteConfirm(UiRuntime& ui);
static void updateFindStatus(UiRuntime& ui);

static const markdown::Document& previewDocument(UiRuntime& ui) {
  const auto& source = ui.editor.text();
  if(!ui.cachedMarkdownDocument || ui.cachedMarkdownSource != source) {
    ui.cachedMarkdownSource = source;
    ui.cachedMarkdownDocument = ui.parser.parse(ui.cachedMarkdownSource);
  }
  return *ui.cachedMarkdownDocument;
}

static void markEdited(UiRuntime& ui) {
  ui.lastEdit = SDL_GetTicks();
  if(ui.state.hasLibrary() && !ui.state.selection().noteId.empty()) {
    if(!ui.state.saveSelectedNoteRecovery(ui.editor.text())) ui.status = "Recovery save failed";
  }
}

// Block transforms arrive as one erase-and-insert, so they land on the editor's
// single undo stack instead of keeping state of their own.
static bool applyEdit(UiRuntime& ui, const doc::Edit& edit) {
  if(!edit.valid) return false;
  ui.editor.replaceRange(edit.start, edit.end, edit.text);
  if(edit.selects) ui.editor.selectRange(edit.anchor, edit.cursor);
  else ui.editor.moveCursor(edit.cursor);
  markEdited(ui);
  ui.revealEditorCursor = true;
  return true;
}

// Runs a transform against the buffer as it stands right now. Chaining these
// with `||` is safe: a transform only sees the buffer when the earlier ones
// declined to change it.
template <typename Transform>
static bool applyTransform(UiRuntime& ui, Transform&& transform) {
  return applyEdit(ui, transform(ui.editor.text(), ui.editor.cursor()));
}

static void wrapEditorSelection(UiRuntime& ui, std::string_view open, std::string_view close, std::string_view label) {
  if(ui.focus != FocusArea::Editor) return;
  const std::size_t start = ui.editor.hasSelection() ? ui.editor.selectionStart() : ui.editor.cursor();
  const std::size_t end = ui.editor.hasSelection() ? ui.editor.selectionEnd() : ui.editor.cursor();
  if(applyEdit(ui, doc::wrapSelection(ui.editor.text(), start, end, open, close))) ui.status = std::string(label);
}

static void linkEditorSelection(UiRuntime& ui) {
  if(ui.focus != FocusArea::Editor) return;
  const std::size_t start = ui.editor.hasSelection() ? ui.editor.selectionStart() : ui.editor.cursor();
  const std::size_t end = ui.editor.hasSelection() ? ui.editor.selectionEnd() : ui.editor.cursor();
  if(applyEdit(ui, doc::makeLink(ui.editor.text(), start, end))) ui.status = "Link: type the destination";
}

static void clearBlockSelection(UiRuntime& ui) {
  ui.blockSelectActive = false;
}

// The blocks a command applies to: the block selection when there is one,
// otherwise the block holding the caret. Both ends are carets, not indices.
static std::pair<std::size_t, std::size_t> blockSelectionCarets(const UiRuntime& ui) {
  if(!ui.blockSelectActive) return {ui.editor.cursor(), ui.editor.cursor()};
  return {std::min(ui.blockSelectAnchor, ui.blockSelectFocus),
          std::max(ui.blockSelectAnchor, ui.blockSelectFocus)};
}

static void selectBlockAtCursor(UiRuntime& ui) {
  const auto blocks = doc::scanBlocks(ui.editor.text());
  std::size_t index = doc::blockIndexAt(blocks, ui.editor.cursor());
  // A blank line - the empty last line included - is a separator, not something
  // to select: step back to the nearest real block.
  while(index > 0 && blocks[index].kind == doc::BlockKind::Blank) --index;
  ui.blockSelectActive = true;
  ui.blockSelectAnchor = blocks[index].start;
  ui.blockSelectFocus = blocks[index].start;
  ui.editor.moveCursor(blocks[index].start);
  ui.editor.clearSelection();
  ui.editor.breakUndoGroup();
}

// A range transform hands back its result as a text selection. A block
// selection wants the same span expressed as blocks again.
static void syncBlockSelectionToEdit(UiRuntime& ui) {
  if(!ui.blockSelectActive) return;
  if(ui.editor.hasSelection()) {
    ui.blockSelectAnchor = ui.editor.selectionStart();
    // One byte inside the last block, not the boundary after it, so the range
    // does not reach into whatever follows.
    ui.blockSelectFocus = ui.editor.selectionEnd() > ui.blockSelectAnchor ? ui.editor.selectionEnd() - 1
                                                                         : ui.blockSelectAnchor;
    ui.editor.clearSelection();
    ui.editor.moveCursor(ui.blockSelectAnchor);
  } else {
    ui.blockSelectAnchor = ui.editor.cursor();
    ui.blockSelectFocus = ui.editor.cursor();
  }
}

// Moves the focus end of a block selection by whole blocks. Blanks are skipped:
// they are separators, not something a user means to select.
static void moveBlockSelection(UiRuntime& ui, int delta, bool extend) {
  const auto blocks = doc::scanBlocks(ui.editor.text());
  std::size_t next = doc::blockIndexAt(blocks, ui.blockSelectFocus);
  bool moved = false;
  while(true) {
    if(delta < 0) {
      if(next == 0) break;
      --next;
    } else {
      if(next + 1 >= blocks.size()) break;
      ++next;
    }
    if(blocks[next].kind != doc::BlockKind::Blank) {
      moved = true;
      break;
    }
  }
  if(!moved) return;
  ui.blockSelectFocus = blocks[next].start;
  if(!extend) ui.blockSelectAnchor = ui.blockSelectFocus;
  ui.editor.moveCursor(blocks[next].start);
  ui.editor.clearSelection();
  ui.revealEditorCursor = true;
}

static void turnCurrentBlockInto(UiRuntime& ui, doc::BlockKind kind, int level, std::string_view label) {
  if(ui.focus != FocusArea::Editor) return;
  const auto [from, to] = blockSelectionCarets(ui);
  if(applyEdit(ui, doc::turnBlocksInto(ui.editor.text(), from, to, kind, level))) {
    syncBlockSelectionToEdit(ui);
    ui.status = std::string(label);
  } else {
    ui.status = "Already " + std::string(label);
  }
}

// Every block shape the block menu, the slash menu and the turn-into submenu
// can produce. One table, so the three stay in step.
struct BlockKindEntry {
  const char* id;
  const char* label;
  const char* detail;
  doc::BlockKind kind;
  int level;
};

static constexpr BlockKindEntry kBlockKinds[] = {
  {"turn:text", "Text", "Plain paragraph", doc::BlockKind::Paragraph, 0},
  {"turn:h1", "Heading 1", "# ", doc::BlockKind::Heading, 1},
  {"turn:h2", "Heading 2", "## ", doc::BlockKind::Heading, 2},
  {"turn:h3", "Heading 3", "### ", doc::BlockKind::Heading, 3},
  {"turn:bullet", "Bulleted list", "- ", doc::BlockKind::Bullet, 0},
  {"turn:ordered", "Numbered list", "1. ", doc::BlockKind::Ordered, 0},
  {"turn:todo", "To-do list", "- [ ] ", doc::BlockKind::Todo, 0},
  {"turn:quote", "Quote", "> ", doc::BlockKind::Quote, 0},
  {"turn:callout", "Callout", "> [!NOTE] ", doc::BlockKind::Callout, 0},
  {"turn:tip", "Tip callout", "> [!TIP] ", doc::BlockKind::Callout, 1},
  {"turn:important", "Important callout", "> [!IMPORTANT] ", doc::BlockKind::Callout, 2},
  {"turn:warning", "Warning callout", "> [!WARNING] ", doc::BlockKind::Callout, 3},
  {"turn:caution", "Caution callout", "> [!CAUTION] ", doc::BlockKind::Callout, 4},
  {"turn:code", "Code block", "```", doc::BlockKind::Code, 0},
  {"turn:divider", "Divider", "---", doc::BlockKind::Divider, 0},
};

static const BlockKindEntry* blockKindFor(std::string_view id) {
  for(const auto& entry : kBlockKinds) {
    if(id == entry.id) return &entry;
  }
  return nullptr;
}

static bool moveSelectedBlocks(UiRuntime& ui, int delta) {
  const auto [from, to] = blockSelectionCarets(ui);
  if(!applyEdit(ui, doc::moveBlocks(ui.editor.text(), from, to, delta))) return false;
  syncBlockSelectionToEdit(ui);
  ui.status = delta < 0 ? "Moved block up" : "Moved block down";
  return true;
}

// The block whose fold would swallow `index`: itself when it heads one, and
// otherwise the nearest one above that reaches down to it.
static std::size_t foldHeadFor(const std::vector<doc::SourceBlock>& blocks, std::size_t index) {
  if(index < blocks.size() && doc::foldable(blocks, index)) return index;
  for(std::size_t i = index; i-- > 0;) {
    if(doc::foldEnd(blocks, i) > index) return i;
  }
  return blocks.size();
}

// Folding changes what is on screen and never the file, so it goes nowhere near
// the editor or the undo stack.
static void toggleFoldAt(UiRuntime& ui, std::size_t caret) {
  const std::string& source = ui.editor.text();
  const auto blocks = doc::scanBlocks(source);
  const std::size_t index = foldHeadFor(blocks, doc::blockIndexAt(blocks, std::min(caret, source.size())));
  if(index >= blocks.size()) {
    ui.status = "Nothing to fold here";
    return;
  }
  const bool folded = ui.folds.toggle(ui.state.selection().noteId, doc::foldKey(source, blocks[index]));
  // A section that just collapsed must not be left holding the caret. Only the
  // blocks it actually hides count: the caret further down the note stays put.
  const std::size_t end = doc::foldEnd(blocks, index);
  const std::size_t caretNow = ui.editor.cursor();
  if(folded && caretNow >= blocks[index].end && caretNow < blocks[end - 1].end) {
    ui.editor.moveCursor(blocks[index].contentEnd);
    ui.editor.clearSelection();
  }
  ui.status = folded ? "Folded" : "Unfolded";
  ui.revealEditorCursor = true;
}

// The one place block commands are dispatched, shared by the block menu, the
// slash menu, the selection toolbar and the keyboard.
static void performBlockCommand(UiRuntime& ui, const std::string& id) {
  if(const auto* entry = blockKindFor(id)) {
    turnCurrentBlockInto(ui, entry->kind, entry->level, entry->label);
    return;
  }
  const auto [from, to] = blockSelectionCarets(ui);
  if(id == "duplicate") {
    if(applyEdit(ui, doc::duplicateBlocks(ui.editor.text(), from, to))) {
      syncBlockSelectionToEdit(ui);
      ui.status = "Duplicated block";
    }
  } else if(id == "delete") {
    if(applyEdit(ui, doc::deleteBlocks(ui.editor.text(), from, to))) {
      syncBlockSelectionToEdit(ui);
      ui.status = "Deleted block";
    }
  } else if(id == "move-up") {
    moveSelectedBlocks(ui, -1);
  } else if(id == "move-down") {
    moveSelectedBlocks(ui, 1);
  } else if(id == "fold") {
    toggleFoldAt(ui, ui.editor.cursor());
  }
}

static bool setClipboardText(std::string_view value) {
  const std::string text {value};
  SDL_ClearError();
  const bool clipboardOk = SDL_SetClipboardText(text.c_str());
  const std::string clipboardError = SDL_GetError();
  SDL_ClearError();
  SDL_SetPrimarySelectionText(text.c_str());
  const bool clipboardHasText = SDL_HasClipboardText();
  const bool primaryHasText = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard set"
              << " bytes=" << text.size()
              << " clipboard_ok=" << clipboardOk
              << " clipboard_has_text=" << clipboardHasText
              << " primary_has_text=" << primaryHasText;
    if(!clipboardOk) std::cerr << " error=\"" << clipboardError << "\"";
    std::cerr << "\n";
  }
  return clipboardOk;
}

static bool publishEditorPrimarySelection(UiRuntime& ui) {
  if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
    const auto selected = ui.editor.selectedText();
    return SDL_SetPrimarySelectionText(selected.c_str());
  }
  return false;
}

static void selectNoteAt(UiRuntime& ui, int index) {
  auto notes = ui.state.currentNotes();
  if(notes.empty()) return;
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  index = std::clamp(index, 0, static_cast<int>(notes.size()) - 1);
  ui.noteCursor = index;
  ui.state.selectNote(notes[static_cast<std::size_t>(index)].id);
  if(auto note = ui.state.selectedNote()) {
    ui.loadedNoteId = note->metadata.id;
    const auto recovered = ui.state.selectedRecoveryBody();
    ui.editor.setText(recovered ? *recovered : note->body);
    if(recovered && *recovered != note->body) ui.editor.markDirty();
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = false;
    ui.status = recovered && *recovered != note->body ? "Recovered unsaved " + note->metadata.title : "Loaded " + note->metadata.title;
    ui.state.noteOpened(note->metadata.id);
  }
}

static void selectNoteById(UiRuntime& ui, const std::string& noteId) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectNote(noteId);
  if(auto note = ui.state.selectedNote()) {
    ui.loadedNoteId = note->metadata.id;
    const auto recovered = ui.state.selectedRecoveryBody();
    ui.editor.setText(recovered ? *recovered : note->body);
    if(recovered && *recovered != note->body) ui.editor.markDirty();
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = false;
    ui.status = recovered && *recovered != note->body ? "Recovered unsaved " + note->metadata.title : "Loaded " + note->metadata.title;
    ui.state.noteOpened(note->metadata.id);
  }
}

static void loadSelectedIntoEditor(UiRuntime& ui) {
  clearBlockSelection(ui);
  if(auto note = ui.state.selectedNote()) {
    if(ui.loadedNoteId != note->metadata.id || !ui.editor.dirty()) {
      ui.loadedNoteId = note->metadata.id;
      ui.editor.setText(note->body);
    }
  }
}

static void createNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Start with --library <path> before creating notes";
    return;
  }
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  const auto folder = ui.state.selection().folder;
  if(auto created = ui.state.createNote("Untitled", folder, "# Untitled\n\n")) {
    ui.loadedNoteId = created->id;
    ui.editor.setText("# Untitled\n\n");
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = true;
    ui.focus = FocusArea::Editor;
    ui.status = "Created " + created->title;
  }
}

static void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  const auto previousFolder = ui.state.selection().folder;
  ui.state.selectFolder(folder);
  createNote(ui);
  if(ui.state.selection().noteId.empty()) ui.state.selectFolder(previousFolder);
}

static bool saveCurrent(UiRuntime& ui, bool quiet) {
  if(!ui.state.hasLibrary()) {
    if(!quiet) ui.status = "No library open";
    return false;
  }
  if(ui.state.selection().noteId.empty()) {
    createNote(ui);
  }
  if(ui.state.saveSelectedNote(ui.editor.text())) {
    ui.editor.markSaved();
    if(!quiet) ui.status = "Saved " + trimTitle(ui.editor.text());
    return true;
  }
  ui.status = quiet ? "Autosave failed" : "Save failed";
  return false;
}

static bool ensureSelectedNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Open a library before attaching files";
    return false;
  }
  if(ui.state.selection().noteId.empty()) createNote(ui);
  return !ui.state.selection().noteId.empty();
}

static void insertAttachmentMarkdown(UiRuntime& ui, const attachments::AttachmentLink& link) {
  if(ui.editor.text().empty() || ui.editor.text().back() == '\n') ui.editor.insert(link.markdown + "\n");
  else ui.editor.insert("\n" + link.markdown + "\n");
  (void)saveCurrent(ui);
}

static bool attachPathToEditor(UiRuntime& ui, const std::filesystem::path& source) {
  if(!ensureSelectedNote(ui)) return false;
  auto selected = ui.state.selectedNote();
  if(!selected) return false;
  attachments::AttachmentService service;
  try {
    const auto link = service.attachFile(ui.state.libraryRoot(), selected->metadata.id, source);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Attached " + source.filename().string();
    return true;
  } catch(const std::exception& error) {
    ui.status = "Attach failed: " + std::string(error.what());
    return false;
  }
}

static bool pasteClipboardImage(UiRuntime& ui) {
  static constexpr const char* kImageMimes[] = {"image/png", "image/jpeg", "image/jpg", "image/bmp", "image/webp"};
  const char* mime = nullptr;
  for(const char* candidate : kImageMimes) {
    if(SDL_HasClipboardData(candidate)) {
      mime = candidate;
      break;
    }
  }
  if(!mime) return false;
  // Only commit to image handling (which may create a note) once we know the
  // clipboard actually holds image data, so a plain-text paste is never hijacked.
  if(!ensureSelectedNote(ui)) return false;

  size_t size = 0;
  void* data = SDL_GetClipboardData(mime, &size);
  if(!data || size == 0) {
    if(data) SDL_free(data);
    ui.status = "Clipboard image data is empty";
    return true;
  }

  auto selected = ui.state.selectedNote();
  if(!selected) {
    SDL_free(data);
    return false;
  }

  attachments::AttachmentService service;
  try {
    const auto link = service.attachBytes(ui.state.libraryRoot(), selected->metadata.id, fileNameForMime(mime), data, size);
    SDL_free(data);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Pasted image attachment";
    return true;
  } catch(const std::exception& error) {
    SDL_free(data);
    ui.status = "Paste image failed: " + std::string(error.what());
    return true;
  }
}

static bool pasteClipboardText(UiRuntime& ui) {
  const bool hasText = SDL_HasClipboardText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard paste editor"
              << " has_text=" << hasText
              << " has_primary=" << SDL_HasPrimarySelectionText()
              << "\n";
  }
  if(!hasText) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "clipboard paste editor bytes=" << std::strlen(raw) << "\n";
  ui.editor.insert(raw);
  markEdited(ui);
  SDL_free(raw);
  return true;
}

static std::string* focusedInput(UiRuntime& ui) {
  switch(ui.focus) {
    case FocusArea::Search: return &ui.searchDraft;
    case FocusArea::Find: return &ui.findDraft;
    default: return nullptr;
  }
}

static void syncFocusedInput(UiRuntime& ui) {
  if(ui.focus == FocusArea::Search) {
    ui.state.setSearch(ui.searchDraft, ui.searchScope);
    selectNoteAt(ui, 0);
  } else if(ui.focus == FocusArea::Find) {
    updateFindStatus(ui);
  }
}

static bool pasteClipboardIntoInput(UiRuntime& ui) {
  auto* input = focusedInput(ui);
  const bool hasText = SDL_HasClipboardText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard paste input"
              << " input=" << (input != nullptr)
              << " has_text=" << hasText
              << " has_primary=" << SDL_HasPrimarySelectionText()
              << "\n";
  }
  if(!input || !hasText) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "clipboard paste input bytes=" << std::strlen(raw) << "\n";
  if(ui.inputAllSelected) input->clear();
  *input += raw;
  ui.inputAllSelected = false;
  SDL_free(raw);
  syncFocusedInput(ui);
  return true;
}

static bool pastePrimarySelectionText(UiRuntime& ui) {
  const bool hasPrimary = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "primary paste editor"
              << " has_primary=" << hasPrimary
              << " has_clipboard=" << SDL_HasClipboardText()
              << "\n";
  }
  if(!hasPrimary) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "primary paste editor bytes=" << std::strlen(raw) << "\n";
  ui.editor.insert(raw);
  markEdited(ui);
  SDL_free(raw);
  return true;
}

static bool pastePrimarySelectionIntoInput(UiRuntime& ui) {
  auto* input = focusedInput(ui);
  const bool hasPrimary = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "primary paste input"
              << " input=" << (input != nullptr)
              << " has_primary=" << hasPrimary
              << " has_clipboard=" << SDL_HasClipboardText()
              << "\n";
  }
  if(!input || !hasPrimary) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "primary paste input bytes=" << std::strlen(raw) << "\n";
  if(ui.inputAllSelected) input->clear();
  *input += raw;
  ui.inputAllSelected = false;
  SDL_free(raw);
  syncFocusedInput(ui);
  return true;
}

static void beginTagEdit(UiRuntime& ui) {
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  auto note = ui.state.selectedNote();
  if(!note) {
    ui.status = "Select a note before editing tags";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "tags";
  overlay.title = "Tags for \"" + note->item.title + "\"";
  overlay.value = joinTags(note->metadata.tags);
  overlay.placeholder = "space separated";
  overlay.valueSelected = !overlay.value.empty();
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void saveTags(UiRuntime& ui) {
  if(ui.state.updateSelectedTags(splitTags(ui.tagDraft))) {
    ui.focus = FocusArea::Editor;
    ui.status = "Saved tags";
  } else {
    ui.status = "No selected note for tags";
  }
}

static void beginRename(UiRuntime& ui) {
  auto note = ui.state.selectedNote();
  if(!note) {
    ui.status = "Select a note before renaming";
    return;
  }
  if(ui.editor.dirty() && !saveCurrent(ui)) return;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "rename-note";
  overlay.title = "Rename note";
  overlay.value = note->metadata.title.empty() ? note->item.title : note->metadata.title;
  overlay.placeholder = "Note title";
  overlay.valueSelected = !overlay.value.empty();
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void saveRename(UiRuntime& ui) {
  if(ui.renameDraft.empty()) {
    ui.status = "Rename needs a title";
    return;
  }
  if(ui.state.renameSelectedNote(ui.renameDraft)) {
    loadSelectedIntoEditor(ui);
    ui.focus = FocusArea::Editor;
    ui.status = "Renamed note";
  } else {
    ui.status = "Rename failed";
  }
}

static void beginFolderCreate(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Open a library before creating notebooks";
    return;
  }
  ui.creatingFolder = true;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "folder-name";
  overlay.title = "New notebook";
  overlay.value = "Notebook";
  overlay.placeholder = "Notebook name";
  overlay.valueSelected = true;
  overlay.hint = "Enter to create, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void beginFolderRename(UiRuntime& ui) {
  if(ui.state.selection().folder.empty()) {
    ui.status = "Root notebook cannot be renamed";
    return;
  }
  ui.creatingFolder = false;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "folder-name";
  overlay.title = "Rename notebook";
  overlay.value = ui.state.selection().folder.generic_string();
  overlay.placeholder = "Notebook name";
  overlay.valueSelected = !overlay.value.empty();
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void saveFolderRename(UiRuntime& ui) {
  if(ui.folderRenameDraft.empty()) {
    ui.status = "Notebook name is required";
    return;
  }
  const bool creating = ui.creatingFolder;
  const bool saved = creating ? ui.state.createFolder(ui.folderRenameDraft) : ui.state.renameSelectedFolder(ui.folderRenameDraft);
  if(saved) {
    ui.creatingFolder = false;
    ui.focus = FocusArea::Folders;
    ui.status = creating ? "Created notebook" : "Saved notebook";
  } else {
    ui.status = "Notebook change failed";
  }
}

static void deleteSelected(UiRuntime& ui) {
  if(ui.state.deleteSelectedNote()) {
    ui.editor.setText("");
    ui.loadedNoteId.clear();
      selectNoteAt(ui, 0);
    ui.status = "Deleted note";
  } else {
    ui.status = "Delete failed";
  }
}

static void deleteSelectedFolder(UiRuntime& ui) {
  if(ui.state.selection().folder.empty()) {
    ui.status = "Root notebook cannot be deleted";
    return;
  }
  if(ui.state.deleteSelectedFolder()) {
    ui.editor.setText("");
    ui.loadedNoteId.clear();
      ui.status = "Deleted notebook";
  } else {
    ui.status = "Notebook delete failed";
  }
}

static const char* paneModeName(ui::PaneMode mode) {
  switch(mode) {
    case ui::PaneMode::Editor: return "Raw Markdown";
    case ui::PaneMode::Viewer: return "Reading";
    case ui::PaneMode::Split: return "Split";
    case ui::PaneMode::Live: break;
  }
  return "Live";
}

static void setPaneMode(UiRuntime& ui, ui::PaneMode mode) {
  clearBlockSelection(ui);
  ui.state.shell().paneMode = mode;
  ui.focus = mode == ui::PaneMode::Viewer ? FocusArea::Viewer : FocusArea::Editor;
  ui.revealEditorCursor = true;
  ui.status = paneModeName(mode);
}

static void cyclePaneMode(UiRuntime& ui) {
  switch(ui.state.shell().paneMode) {
    case ui::PaneMode::Live: setPaneMode(ui, ui::PaneMode::Editor); break;
    case ui::PaneMode::Editor: setPaneMode(ui, ui::PaneMode::Viewer); break;
    case ui::PaneMode::Viewer: setPaneMode(ui, ui::PaneMode::Split); break;
    case ui::PaneMode::Split: setPaneMode(ui, ui::PaneMode::Live); break;
  }
}

static void updateFindStatus(UiRuntime& ui) {
  if(ui.findDraft.empty()) {
    ui.status = "Find in note";
    return;
  }
  std::size_t count = 0;
  std::size_t pos = ui.editor.text().find(ui.findDraft);
  while(pos != std::string::npos) {
    ++count;
    pos = ui.editor.text().find(ui.findDraft, pos + std::max<std::size_t>(1, ui.findDraft.size()));
  }
  ui.status = std::to_string(count) + " matches in note";
}

static std::string searchScopeLabel(library::SearchScope scope) {
  switch(scope) {
    case library::SearchScope::All: return "A";
    case library::SearchScope::Title: return "T";
    case library::SearchScope::Content: return "C";
  }
  return "A";
}

static library::SearchScope nextSearchScope(library::SearchScope scope) {
  switch(scope) {
    case library::SearchScope::All: return library::SearchScope::Title;
    case library::SearchScope::Title: return library::SearchScope::Content;
    case library::SearchScope::Content: return library::SearchScope::All;
  }
  return library::SearchScope::All;
}

static void performAction(UiRuntime& ui, UiAction action) {
  switch(action) {
    case UiAction::Refresh:
      ui.state.refreshLibrary();
      ui.status = "Refreshed library";
      break;
    case UiAction::NewNote:
      createNote(ui);
      break;
    case UiAction::RenameNote:
      beginRename(ui);
      break;
    case UiAction::DeleteNote:
      openDeleteNoteConfirm(ui);
      break;
    case UiAction::Save:
      (void)saveCurrent(ui);
      break;
    case UiAction::Tags:
      beginTagEdit(ui);
      break;
    case UiAction::PaneEditor:
      ui.state.shell().paneMode = ui::PaneMode::Editor;
      ui.focus = FocusArea::Editor;
      break;
    case UiAction::PaneViewer:
      ui.state.shell().paneMode = ui::PaneMode::Viewer;
      ui.focus = FocusArea::Viewer;
      break;
    case UiAction::PaneSplit:
      ui.state.shell().paneMode = ui::PaneMode::Split;
      ui.focus = FocusArea::Editor;
      break;
  }
}

static std::filesystem::path uiStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "ui.state";
}

static std::filesystem::path foldStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "folds.state";
}

static std::filesystem::path treeStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "tree.state";
}

static std::filesystem::path libraryPathConfigPath() {
  return microcore::platform::resolveRuntimePaths().configDir / "library-path";
}

static std::optional<std::filesystem::path> readConfiguredLibraryRoot() {
  std::ifstream in(libraryPathConfigPath());
  if(!in) return std::nullopt;
  std::string line;
  std::getline(in, line);
  if(line.empty()) return std::nullopt;
  return std::filesystem::path(line);
}

static bool writeConfiguredLibraryRoot(const std::filesystem::path& root) {
  const auto configPath = libraryPathConfigPath();
  return platform::writeFileDurably(configPath, root.generic_string() + "\n");
}

// Everything about a library that lives outside its notes. Written when the
// app closes, and again before it opens a different library, so the view state
// of the one being left is never spent on the one being opened.
static void persistLibraryState(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) return;
  ui.state.saveUiState(uiStatePath(ui.state.libraryRoot()));
  if(ui.folds.dirty()) ui.folds.save(foldStatePath(ui.state.libraryRoot()));
  if(ui.tree.dirty()) {
    platform::writeFileDurably(treeStatePath(ui.state.libraryRoot()), ui.tree.serialize());
  }
}

// Opens a library and restores the view state stored beside it. The one path
// into a library, taken by startup and by the settings dialog alike, so a
// library opened from inside the app comes up exactly as it would on the next
// launch.
static bool openLibraryRoot(UiRuntime& ui, const std::filesystem::path& root) {
  if(!ui.state.openOrCreateLibrary(root)) return false;
  ui.state.loadUiState(uiStatePath(ui.state.libraryRoot()));
  ui.folds.load(foldStatePath(ui.state.libraryRoot()));
  std::ostringstream treeBuffer;
  if(std::ifstream treeState(treeStatePath(ui.state.libraryRoot())); treeState) {
    treeBuffer << treeState.rdbuf();
  }
  ui.tree.load(treeBuffer.str());
  // Whatever was open last time still has to be reachable, so the folder
  // holding it is opened even if its parent was left collapsed.
  ui.tree.reveal(ui.state.selection().folder);
  ui.searchDraft = ui.state.selection().search;
  ui.searchScope = ui.state.selection().searchScope;
  // The editor is emptied first: the previous library's note is gone, and a
  // library with nothing in it has no note to overwrite it with.
  ui.loadedNoteId.clear();
  ui.editor.setText("");
  ui.editor.markSaved();
  ui.sidebarScroll = 0;
  ui.editorScroll = 0;
  ui.viewerScroll = 0;
  loadSelectedIntoEditor(ui);
  if(ui.state.selection().noteId.empty()) selectNoteAt(ui, 0);
  return true;
}

static bool attachFromCli(UiRuntime& ui, const std::filesystem::path& source) {
  if(source.empty()) return true;
  if(!ui.state.hasLibrary()) {
    std::cerr << "--attach requires --library\n";
    return false;
  }
  ui.state.loadUiState(uiStatePath(ui.state.libraryRoot()));
  auto selected = ui.state.selectedNote();
  if(!selected) {
    std::cerr << "--attach requires a selected note saved in UI state\n";
    return false;
  }
  attachments::AttachmentService service;
  try {
    const auto link = service.attachFile(ui.state.libraryRoot(), selected->metadata.id, source);
    ui.editor.setText(selected->body);
    ui.editor.insert("\n" + link.markdown + "\n");
    ui.state.saveSelectedNote(ui.editor.text());
    std::cout << link.markdown << "\n";
    return true;
  } catch(const std::exception& error) {
    std::cerr << "attach failed: " << error.what() << "\n";
    return false;
  }
}

static bool hovered(const UiRuntime& ui, Rect rect) {
  return contains(rect, ui.mouseX, ui.mouseY);
}

static void drawSectionLabel(TextRenderer& text, std::string_view label, float x, float y) {
  text.draw(label, x, y, theme().dim);
}

// An empty place says what it is, what to do about it, and which keys do that.
// The third line is what turns a dead end into an offer, so it is dimmer than
// the rest rather than left out.
static void drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail, Rect rect,
                             std::string_view keys = {}) {
  const ui::TextStyle titleStyle {ui::FontFamily::Sans, true, false, ui::type().ui};
  const ui::TextStyle bodyStyle {ui::FontFamily::Sans, false, false, ui::type().small};
  const ui::TextStyle keyStyle {ui::FontFamily::Sans, false, false, ui::type().tiny};
  const int room = static_cast<int>(std::max(60.0f, rect.w - 36.0f));
  float y = rect.y + 14.0f;
  text.draw(ellipsizeToWidth(text, std::string(title), room, titleStyle), rect.x + 18.0f, y, theme().text, titleStyle);
  y += static_cast<float>(text.lineHeight(titleStyle)) + 6.0f;
  text.draw(ellipsizeToWidth(text, std::string(detail), room, bodyStyle), rect.x + 18.0f, y, theme().muted, bodyStyle);
  if(keys.empty()) return;
  y += static_cast<float>(text.lineHeight(bodyStyle)) + 8.0f;
  text.draw(ellipsizeToWidth(text, std::string(keys), room, keyStyle), rect.x + 18.0f, y, theme().dim, keyStyle);
}

static void drawFindHighlights(SDL_Renderer* renderer, TextRenderer& text, const UiRuntime& ui, const std::string& line, Rect writing, float y) {
  if(ui.findDraft.empty()) return;
  std::size_t pos = line.find(ui.findDraft);
  while(pos != std::string::npos) {
    const auto prefix = std::string_view(line.data(), pos);
    const float x = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
    const float w = static_cast<float>(std::max(6, text.width(ui.findDraft, false, true)));
    if(x < writing.x + writing.w - 8) {
      fill(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, theme().findBg);
      stroke(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, theme().findBorder);
    }
    pos = line.find(ui.findDraft, pos + std::max<std::size_t>(1, ui.findDraft.size()));
  }
}

static void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return;
  Rect track {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, std::max(24.0f, viewport.h - 18.0f)};
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  Rect thumb {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
  fill(renderer, track, theme().scrollTrack);
  fill(renderer, thumb, theme().scrollThumb);
  stroke(renderer, thumb, theme().scrollThumbBorder);
}

static Rect scrollbarTrack(Rect viewport) {
  const float trackH = std::max(24.0f, viewport.h - 18.0f);
  return {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, trackH};
}

static Rect scrollbarThumb(Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return {};
  const auto track = scrollbarTrack(viewport);
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  return {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
}

static Rect scrollbarHitRect(Rect thumb) {
  return {thumb.x - 7.0f, thumb.y - 2.0f, thumb.w + 14.0f, thumb.h + 4.0f};
}

static int scrollFromThumbY(Rect viewport, float y, float dragOffsetY, int maxScroll) {
  const auto track = scrollbarTrack(viewport);
  const auto thumb = scrollbarThumb(viewport, 0, maxScroll);
  const float range = std::max(1.0f, track.h - thumb.h);
  const float t = std::clamp((y - dragOffsetY - track.y) / range, 0.0f, 1.0f);
  return static_cast<int>(std::round(t * static_cast<float>(maxScroll)));
}

static Rect searchBoxRect(Rect notes) {
  return {notes.x + 14.0f, notes.y + 12.0f, notes.w - 28.0f, 34.0f};
}

static Rect editorWritingRect(Rect editorRect) {
  return {editorRect.x + 8.0f, editorRect.y + 8.0f, editorRect.w - 16.0f, editorRect.h - 28.0f};
}

static Rect viewerPageRect(Rect viewerRect) {
  return {viewerRect.x + 8.0f, viewerRect.y + 8.0f, viewerRect.w - 16.0f, viewerRect.h - 28.0f};
}

static bool isResizeGutter(const AppLayout& layout, float x, float y) {
  if(y < layout.sidebar.y || y > layout.sidebar.y + layout.sidebar.h) return false;
  return std::abs(x - (layout.sidebar.x + layout.sidebar.w)) <= 4.0f ||
         std::abs(x - (layout.notes.x + layout.notes.w)) <= 4.0f;
}

static bool noteRowAt(const UiRuntime& ui, Rect notesRect, float x, float y) {
  if(!contains(notesRect, x, y)) return false;
  float rowY = notesRect.y + 62.0f;
  if(!ui.searchDraft.empty()) {
    for(const auto& result : ui.state.currentSearchResults()) {
      const std::size_t snippetCount = std::max<std::size_t>(result.snippets.size(), result.matchLine.empty() ? 0 : 1);
      const float availableH = notesRect.y + notesRect.h - 24.0f - (rowY - 8.0f);
      const std::size_t maxVisibleSnippets = availableH <= 90.0f ? 0 : static_cast<std::size_t>((availableH - 30.0f) / 60.0f);
      const std::size_t visibleSnippets = std::min<std::size_t>(snippetCount, std::min<std::size_t>(4, maxVisibleSnippets));
      const float rowH = visibleSnippets > 0 ? 30.0f + static_cast<float>(visibleSnippets * 60) : 50.0f;
      Rect row {notesRect.x + 10.0f, rowY - 8.0f, notesRect.w - 20.0f, rowH};
      if(contains(row, x, y)) return true;
      rowY += rowH;
    }
    return false;
  }
  const auto notes = ui.state.currentNotes();
  for(std::size_t i = 0; i < notes.size() && rowY < notesRect.y + notesRect.h - 24.0f; ++i) {
    Rect row {notesRect.x + 10.0f, rowY - 8.0f, notesRect.w - 20.0f, 50.0f};
    if(contains(row, x, y)) return true;
    rowY += 50.0f;
  }
  return false;
}

static std::string blockText(const markdown::Block& block) {
  std::string out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) {
      continue;
    } else if(inlineItem.type == markdown::InlineType::Link) {
      out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      out += block.type == markdown::BlockType::Code ? inlineItem.text : "`" + inlineItem.text + "`";
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      out += "[" + inlineItem.text + "]";
    } else {
      out += inlineItem.text;
    }
  }
  return out;
}

static std::vector<std::string> codeBlockLines(const markdown::Block& block) {
  auto lines = splitLines(blockText(block));
  if(lines.size() > 1 && lines.back().empty()) lines.pop_back();
  if(lines.empty()) lines.emplace_back();
  return lines;
}

static std::vector<markdown::Inline> blockImages(const markdown::Block& block) {
  std::vector<markdown::Inline> out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) out.push_back(inlineItem);
  }
  return out;
}

struct InlineRun {
  std::string text;
  std::string target;
  SDL_Color color = theme().text;
  bool mono = false;
  bool strong = false;
  bool emphasis = false;
  bool strikethrough = false;
};

static std::vector<InlineRun> inlineRuns(const std::vector<markdown::Inline>& inlines, SDL_Color baseColor = theme().text) {
  std::vector<InlineRun> runs;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    InlineRun run;
    run.text = inlineItem.text;
    run.color = baseColor;
    run.strong = inlineItem.strong;
    run.emphasis = inlineItem.emphasis;
    run.strikethrough = inlineItem.strikethrough;
    if(inlineItem.type == markdown::InlineType::Link) {
      run.text = inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
      run.target = inlineItem.target;
      run.color = theme().accent;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      run.mono = true;
      run.color = theme().warn;
    } else if(inlineItem.type == markdown::InlineType::Emphasis) {
      run.emphasis = true;
    } else if(inlineItem.type == markdown::InlineType::Strong) {
      run.strong = true;
    } else if(inlineItem.type == markdown::InlineType::Strikethrough) {
      run.strikethrough = true;
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      run.text = "[" + inlineItem.text + "]";
      run.target = "#fn-" + inlineItem.text;
      run.color = theme().accent;
    } else if(inlineItem.type == markdown::InlineType::Html) {
      run.color = theme().dim;
    }
    if(!run.text.empty()) runs.push_back(std::move(run));
  }
  return runs;
}

static std::vector<InlineRun> inlineRuns(const markdown::Block& block, SDL_Color baseColor = theme().text) {
  return inlineRuns(block.inlines, baseColor);
}

// One layout token. Whitespace between tokens is recorded as a flag rather than
// kept as text, so a run boundary never invents a space that the source did not
// have (the "[link](url)." case) and never drops one that it did.
struct LaidWord {
  const InlineRun* run = nullptr;
  std::string text;
  bool spaceBefore = false;
  bool lineBreak = false;
};

static std::vector<LaidWord> layoutWords(const std::vector<InlineRun>& runs) {
  std::vector<LaidWord> out;
  bool pendingSpace = false;
  bool atStart = true;
  std::string token;
  const InlineRun* tokenRun = nullptr;

  auto flush = [&]() {
    if(token.empty()) return;
    LaidWord word;
    word.run = tokenRun;
    word.text = token;
    word.spaceBefore = pendingSpace && !atStart;
    out.push_back(std::move(word));
    token.clear();
    pendingSpace = false;
    atStart = false;
  };

  for(const auto& run : runs) {
    for(const char c : run.text) {
      if(c == '\n') {
        flush();
        LaidWord br;
        br.run = &run;
        br.lineBreak = true;
        out.push_back(std::move(br));
        pendingSpace = false;
        atStart = true;
      } else if(std::isspace(static_cast<unsigned char>(c))) {
        flush();
        pendingSpace = true;
      } else {
        if(token.empty()) tokenRun = &run;
        token.push_back(c);
      }
    }
    flush();
  }
  flush();
  return out;
}

static ui::TextStyle runStyle(const InlineRun& run, float size) {
  ui::TextStyle style;
  style.family = run.mono ? ui::FontFamily::Mono : ui::FontFamily::Sans;
  style.strong = run.strong;
  style.italic = run.emphasis;
  style.size = size;
  return style;
}

static int measureInlineLines(TextRenderer& text, const std::vector<InlineRun>& runs, int maxWidth, float size) {
  int lines = 1;
  int x = 0;
  for(const auto& word : layoutWords(runs)) {
    if(word.lineBreak) {
      ++lines;
      x = 0;
      continue;
    }
    const auto style = runStyle(*word.run, size);
    const int wordW = text.width(word.text, style);
    const int spaceW = (x == 0 || !word.spaceBefore) ? 0 : text.width(" ", style);
    if(x > 0 && x + spaceW + wordW > maxWidth) {
      ++lines;
      x = wordW;
    } else {
      x += spaceW + wordW;
    }
  }
  return std::max(1, lines);
}

static float drawInlineRuns(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>* links, const std::vector<InlineRun>& runs, float x, float y, int maxWidth, int lineStep, float size) {
  float cursorX = x;
  float cursorY = y;
  // Remembers where the previous word of the same link ended, so the underline
  // runs through the spaces inside a multi-word link instead of breaking up.
  const InlineRun* previousRun = nullptr;
  float previousEndX = 0.0f;
  float previousY = -1.0f;
  for(const auto& word : layoutWords(runs)) {
    if(word.lineBreak) {
      cursorX = x;
      cursorY += static_cast<float>(lineStep);
      previousRun = nullptr;
      continue;
    }
    const auto& run = *word.run;
    const auto style = runStyle(run, size);
    const int lineH = text.lineHeight(style);
    const int wordW = text.width(word.text, style);
    const int spaceW = (cursorX == x || !word.spaceBefore) ? 0 : text.width(" ", style);
    if(cursorX > x && cursorX + static_cast<float>(spaceW + wordW) > x + static_cast<float>(maxWidth)) {
      cursorX = x;
      cursorY += static_cast<float>(lineStep);
    } else {
      cursorX += static_cast<float>(spaceW);
    }
    text.draw(word.text, cursorX, cursorY, run.color, style);
    if(run.strikethrough) {
      const float lineY = cursorY + static_cast<float>(lineH) * 0.55f;
      hLine(renderer, cursorX, cursorX + static_cast<float>(wordW), lineY, run.color);
    }
    if(!run.target.empty()) {
      const bool continues = previousRun == &run && std::abs(previousY - cursorY) < 0.5f;
      const float underlineFrom = continues ? previousEndX : cursorX;
      hLine(renderer, underlineFrom, cursorX + static_cast<float>(wordW), cursorY + static_cast<float>(lineH - 2), theme().accentDim);
      if(links) {
        links->push_back({{underlineFrom, cursorY, cursorX + static_cast<float>(wordW) - underlineFrom, static_cast<float>(lineH)}, run.target});
      }
    }
    cursorX += static_cast<float>(wordW);
    previousRun = run.target.empty() ? nullptr : &run;
    previousEndX = cursorX;
    previousY = cursorY;
  }
  return cursorY;
}

static std::vector<std::string> wrapText(TextRenderer& text, std::string_view value, int maxWidth, bool heading = false, bool mono = false) {
  std::vector<std::string> out;
  if(maxWidth <= 0) {
    out.emplace_back(value);
    return out;
  }
  std::istringstream logicalLines {std::string(value)};
  std::string logicalLine;
  while(std::getline(logicalLines, logicalLine)) {
    if(logicalLine.empty()) {
      out.emplace_back();
      continue;
    }
    std::string line;
    std::istringstream words {logicalLine};
    std::string word;
    while(words >> word) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if(!line.empty() && text.width(candidate, heading, mono) > maxWidth) {
        out.push_back(line);
        line = word;
        while(text.width(line, heading, mono) > maxWidth && line.size() > 1) {
          std::string chunk = line;
          while(chunk.size() > 1 && text.width(chunk, heading, mono) > maxWidth) chunk.pop_back();
          out.push_back(chunk);
          line.erase(0, chunk.size());
        }
      } else {
        line = candidate;
      }
    }
    out.push_back(line);
  }
  if(out.empty()) out.emplace_back();
  return out;
}

static std::string inlinePlainText(const std::vector<markdown::Inline>& inlines) {
  std::string out;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    if(inlineItem.type == markdown::InlineType::Link) out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    else if(inlineItem.type == markdown::InlineType::FootnoteRef) out += "[" + inlineItem.text + "]";
    else out += inlineItem.text;
  }
  return out;
}

static std::string anchorFor(std::string value) {
  std::string out;
  bool pendingDash = false;
  for(unsigned char c : value) {
    if(std::isalnum(c)) {
      if(pendingDash && !out.empty()) out.push_back('-');
      out.push_back(static_cast<char>(std::tolower(c)));
      pendingDash = false;
    } else if(!out.empty()) {
      pendingDash = true;
    }
  }
  return out;
}

// A block's own text style. Headings scale by level; code uses the mono face.
// Notion caps the reading measure so long lines stay readable; extra width
// becomes margin rather than more characters per line.
static void contentColumn(Rect page, float& left, float& width) {
  const float available = std::max(80.0f, page.w - 28.0f);
  width = std::min(available, ui::pageWidthPx());
  left = page.x + std::round((page.w - width) / 2.0f);
}

static ui::TextStyle blockTextStyle(const markdown::Block& block) {
  ui::TextStyle style;
  style.family = block.type == markdown::BlockType::Code ? ui::FontFamily::Mono : ui::FontFamily::Sans;
  style.strong = block.type == markdown::BlockType::Heading;
  if(block.type == markdown::BlockType::Heading) style.size = ui::headingSize(block.level);
  else if(block.type == markdown::BlockType::Code) style.size = ui::type().mono;
  else style.size = ui::type().body;
  return style;
}

// Baseline-to-baseline distance. The font's own height is roughly 1.2x, which
// reads too tight for body copy, so the type scale's ratio wins when larger.
static int lineStepFor(TextRenderer& text, const ui::TextStyle& style, float ratio) {
  const float logical = style.size > 0.0f ? style.size : ui::type().body;
  const int fromRatio = static_cast<int>(std::lround(logical * text.displayScale() * ratio));
  return std::max(text.lineHeight(style), fromRatio);
}

static int blockLineStep(TextRenderer& text, const markdown::Block& block) {
  const auto style = blockTextStyle(block);
  const bool heading = block.type == markdown::BlockType::Heading;
  return lineStepFor(text, style, heading ? 1.25f : ui::type().lineHeightRatio);
}

static float listMarkerWidth(const markdown::Block& block) {
  if(block.type == markdown::BlockType::OrderedItem) return 26.0f;
  if(block.type == markdown::BlockType::UnorderedItem) return 18.0f;
  return 0.0f;
}

static float blockBottomSpacing(const markdown::Block& block, bool heading, bool html) {
  if(block.type == markdown::BlockType::BlankLine) return 0.0f;
  if(block.type == markdown::BlockType::OrderedItem || block.type == markdown::BlockType::UnorderedItem) return 2.0f;
  return heading ? 14.0f : html ? 8.0f : 10.0f;
}

static float admonitionLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Admonition) return 0.0f;
  const auto label = block.admonitionType.empty() ? "note" : block.admonitionType;
  return static_cast<float>(text.width(label, false, false, true)) + 18.0f;
}

static float footnoteLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Footnote) return 0.0f;
  const auto label = "[" + (block.footnoteLabel.empty() ? std::string("*") : block.footnoteLabel) + "]";
  return static_cast<float>(text.width(label)) + 12.0f;
}

static std::string imagePlaceholder(const markdown::Inline& image, const UiRuntime& ui, attachments::AttachmentService& attachmentService) {
  if(isRemoteTarget(image.target) || !ui.state.hasLibrary()) return "[remote image skipped: " + image.target + "]";
  try {
    const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
    if(!attachmentService.isSupportedImage(path)) return "[image link: " + image.target + "]";
  } catch(const std::exception&) {
    return "[unsafe image path]";
  }
  return "[image unavailable: " + image.target + "]";
}

static float imagePlaceholderHeight(TextRenderer& text, std::string_view placeholder, float width) {
  const auto lines = wrapText(text, placeholder, static_cast<int>(width), false, true);
  return static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight() + 2) + 8);
}

static float tableHeight(TextRenderer& text, const markdown::Block& block, float width) {
  const float rowPadY = 8.0f;
  const int cols = std::max(1, [&]() {
    int count = 0;
    for(const auto& row : block.tableRows) count = std::max(count, static_cast<int>(row.cells.size()));
    return count;
  }());
  const float cellW = std::max(48.0f, (width - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float h = 0.0f;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) {
      rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f), ui::type().body));
    }
    h += static_cast<float>(rowLines * (text.lineHeight() + 2)) + rowPadY * 2.0f;
  }
  return h + 10.0f;
}

static void drawTable(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>& links, const markdown::Block& block, Rect rect) {
  int cols = 0;
  for(const auto& row : block.tableRows) cols = std::max(cols, static_cast<int>(row.cells.size()));
  if(cols <= 0) return;
  const float cellW = std::max(48.0f, (rect.w - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float y = rect.y;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) {
      rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f), ui::type().body));
    }
    const float rowH = static_cast<float>(rowLines * (text.lineHeight() + 2)) + 16.0f;
    float x = rect.x;
    for(int i = 0; i < cols; ++i) {
      const markdown::TableCell* cell = i < static_cast<int>(row.cells.size()) ? &row.cells[static_cast<std::size_t>(i)] : nullptr;
      Rect cellRect {x, y, cellW, rowH};
      fill(renderer, cellRect, row.header ? theme().tableHeaderBg : theme().tableCellBg);
      stroke(renderer, cellRect, theme().divider);
      if(cell) {
        auto runs = inlineRuns(cell->inlines, row.header ? theme().text : theme().muted);
        const auto cellText = inlinePlainText(cell->inlines);
        float textX = x + 7.0f;
        if(cell->align == markdown::Align::Right) {
          textX = std::max(textX, x + cellW - 7.0f - static_cast<float>(text.width(cellText)));
        } else if(cell->align == markdown::Align::Center) {
          textX = std::max(textX, x + (cellW - static_cast<float>(text.width(cellText))) / 2.0f);
        }
        drawInlineRuns(renderer, text, &links, runs, textX, y + 8.0f, static_cast<int>(cellW - 14.0f), lineStepFor(text, ui::TextStyle {}, ui::type().lineHeightRatio), ui::type().body);
      }
      x += cellW;
    }
    y += rowH;
  }
}

static int viewerMaxScroll(TextRenderer& text, UiRuntime& ui, Rect rect) {
  Rect page {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  attachments::AttachmentService attachmentService;
  const auto& doc = previewDocument(ui);
  const float contentTop = page.y + 14.0f;
  float contentLeft = 0.0f;
  float contentWidth = 0.0f;
  contentColumn(page, contentLeft, contentWidth);
  float measureY = contentTop;
  for(const auto& block : doc.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const bool rule = block.type == markdown::BlockType::HorizontalRule;
    const bool table = block.type == markdown::BlockType::Table;
    const bool html = block.type == markdown::BlockType::Html;
    const bool blankLine = block.type == markdown::BlockType::BlankLine;
    const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
    const float markerW = listMarkerWidth(block);
    const float quoteW = block.type == markdown::BlockType::Quote ? 16.0f : 0.0f;
    const ui::TextStyle blockStyle = blockTextStyle(block);
    if(blankLine) {
      measureY += static_cast<float>(text.lineHeight());
    } else if(rule) {
      measureY += 22.0f;
    } else if(table) {
      measureY += tableHeight(text, block, contentWidth - indentW) + 12.0f;
    } else if(code) {
      const auto lines = codeBlockLines(block);
      measureY += static_cast<float>(std::max<std::size_t>(1, lines.size()) * lineStepFor(text, blockStyle, 1.5f)) + 18.0f;
    } else {
      const auto value = blockText(block);
      if(!value.empty()) {
        const auto runs = inlineRuns(block, theme().text);
        const int lineStep = blockLineStep(text, block);
        const float chromeW = markerW + quoteW + indentW + admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
        measureY += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(contentWidth - chromeW), blockStyle.size) * lineStep) + blockBottomSpacing(block, heading, html);
      }
      for(const auto& image : blockImages(block)) {
        measureY += imagePlaceholderHeight(text, imagePlaceholder(image, ui, attachmentService), contentWidth);
      }
    }
  }
  return std::max(0, static_cast<int>(std::ceil(measureY - contentTop - page.h + 24.0f)));
}

// The tree is drawn as a flat list: one row height, one indent per level, and
// the nesting carried entirely by that indent.
constexpr float kSidebarRowHeight = 26.0f;
constexpr float kSidebarTagHeight = 24.0f;
constexpr float kSidebarLabelHeight = 34.0f;
constexpr float kSidebarIndent = 13.0f;

// Rebuilt every frame from the library rather than cached: it is a few hundred
// rows, and a tree that disagrees with the files is a worse problem than one
// that is rebuilt. Hit-testing then reads the same geometry the draw produced,
// which is how the gutter affordances already work.
static void buildSidebarRows(UiRuntime& ui, Rect rect) {
  ui.sidebarRows.clear();
  const float top = rect.y + 12.0f;
  float y = top - static_cast<float>(ui.sidebarScroll);

  const auto pushLabel = [&](std::string label) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::SectionLabel;
    row.label = std::move(label);
    row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, kSidebarLabelHeight};
    ui.sidebarRows.push_back(std::move(row));
    y += kSidebarLabelHeight;
  };
  const auto pushTreeRow = [&](ui::TreeRow tree) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::Tree;
    row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, kSidebarRowHeight};
    if(tree.expandable) {
      row.disclosure = {rect.x + 10.0f + static_cast<float>(tree.depth) * kSidebarIndent, y + 5.0f, 16.0f, 16.0f};
    }
    row.tree = std::move(tree);
    ui.sidebarRows.push_back(std::move(row));
    y += kSidebarRowHeight;
  };

  const auto notes = ui.state.allNotes();
  const auto root = ui.state.libraryRoot();
  // A shortcut list is a flat list of notes, drawn with the same row the tree
  // uses so a note looks and behaves the same wherever it is listed.
  const auto pushNoteShortcuts = [&](const std::vector<std::string>& ids, std::size_t limit) {
    std::size_t drawn = 0;
    for(const auto& id : ids) {
      if(drawn >= limit) break;
      const auto found = std::find_if(notes.begin(), notes.end(), [&](const auto& note) { return note.id == id; });
      if(found == notes.end()) continue;
      ui::TreeRow tree;
      tree.kind = ui::TreeRowKind::Note;
      tree.depth = 0;
      tree.folder = found->path.lexically_relative(root).parent_path();
      tree.noteId = found->id;
      tree.label = found->title;
      tree.icon = found->icon;
      pushTreeRow(std::move(tree));
      ++drawn;
    }
    return drawn;
  };

  if(!ui.state.shell().favorites.empty()) {
    const std::size_t before = ui.sidebarRows.size();
    pushLabel("FAVORITES");
    if(pushNoteShortcuts(ui.state.shell().favorites, 8) == 0) {
      // Every favourite has been deleted since; the heading would be a lie.
      ui.sidebarRows.resize(before);
      y -= kSidebarLabelHeight;
    }
  }

  for(auto& row : ui.tree.rows(ui.state.folders(), notes, root)) pushTreeRow(std::move(row));

  const auto tags = ui.state.tags();
  if(!tags.empty()) {
    // Tags are a filter over the tree, not a second way to organise it, so they
    // sit below it and read quieter.
    pushLabel("TAGS");
    for(const auto& tag : tags) {
      SidebarRow row;
      row.kind = SidebarRow::Kind::Tag;
      row.rect = {rect.x + 8.0f, y, rect.w - 16.0f, kSidebarTagHeight};
      row.tag = tag;
      ui.sidebarRows.push_back(std::move(row));
      y += kSidebarTagHeight;
    }
  }

  if(!ui.state.shell().recents.empty()) {
    const std::size_t before = ui.sidebarRows.size();
    pushLabel("RECENT");
    if(pushNoteShortcuts(ui.state.shell().recents, 5) == 0) {
      ui.sidebarRows.resize(before);
      y -= kSidebarLabelHeight;
    }
  }

  const float contentHeight = y + static_cast<float>(ui.sidebarScroll) - top;
  ui.sidebarMaxScroll = std::max(0, static_cast<int>(std::ceil(contentHeight - (rect.h - 24.0f))));
  ui.sidebarScroll = std::clamp(ui.sidebarScroll, 0, ui.sidebarMaxScroll);
}

// The row under the pointer, or nothing when the pointer is off the list.
static std::optional<std::size_t> sidebarRowAt(const UiRuntime& ui, Rect sidebar, float x, float y) {
  if(!contains(sidebar, x, y)) return std::nullopt;
  for(std::size_t i = 0; i < ui.sidebarRows.size(); ++i) {
    if(ui.sidebarRows[i].kind == SidebarRow::Kind::SectionLabel) continue;
    if(contains(ui.sidebarRows[i].rect, x, y)) return i;
  }
  return std::nullopt;
}

// One place where a sidebar row turns into a selection, so a click, an arrow
// key and a drop can never disagree about what selecting a row means.
static void activateSidebarRow(UiRuntime& ui, const SidebarRow& row, bool expandFolder) {
  if(row.kind == SidebarRow::Kind::Tag) {
    if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
    ui.state.selectTag(row.tag);
    selectNoteAt(ui, 0);
    return;
  }
  if(row.kind != SidebarRow::Kind::Tree) return;
  if(row.tree.kind == ui::TreeRowKind::Note) {
    selectNoteById(ui, row.tree.noteId);
    // Opening a note from the tree moves the context to its folder too, so the
    // note list and the breadcrumb agree with what is on screen. A search owns
    // the note list while it is running, so it is left alone.
    if(ui.searchDraft.empty() && ui.state.selection().noteId == row.tree.noteId) {
      ui.state.selectFolder(row.tree.folder);
    }
    return;
  }
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.state.selectFolder(row.tree.folder);
  // Clicking a notebook opens it; arrowing onto one only selects it, or holding
  // Down would unfold the whole library on the way past.
  if(expandFolder) ui.tree.setExpanded(row.tree.folder, true);
  selectNoteAt(ui, 0);
}

// Scrolls the cursor row into view using last frame's geometry, which is all
// that is needed to know whether it is off an edge and by how much.
static void revealSidebarRow(UiRuntime& ui, std::size_t index) {
  if(index >= ui.sidebarRows.size() || ui.sidebarRect.h <= 0.0f) return;
  const Rect row = ui.sidebarRows[index].rect;
  const float top = ui.sidebarRect.y + 8.0f;
  const float bottom = ui.sidebarRect.y + ui.sidebarRect.h - 8.0f;
  if(row.y < top) ui.sidebarScroll -= static_cast<int>(std::ceil(top - row.y));
  else if(row.y + row.h > bottom) ui.sidebarScroll += static_cast<int>(std::ceil(row.y + row.h - bottom));
  ui.sidebarScroll = std::clamp(ui.sidebarScroll, 0, ui.sidebarMaxScroll);
}

static void moveTreeCursor(UiRuntime& ui, int delta) {
  if(ui.sidebarRows.empty()) return;
  int index = std::clamp(ui.folderCursor, 0, static_cast<int>(ui.sidebarRows.size()) - 1) + delta;
  // Section labels are drawn, not selectable, so the cursor steps over them.
  while(index >= 0 && index < static_cast<int>(ui.sidebarRows.size()) &&
        ui.sidebarRows[static_cast<std::size_t>(index)].kind == SidebarRow::Kind::SectionLabel) {
    index += delta;
  }
  if(index < 0 || index >= static_cast<int>(ui.sidebarRows.size())) return;
  ui.folderCursor = index;
  revealSidebarRow(ui, static_cast<std::size_t>(index));
  activateSidebarRow(ui, ui.sidebarRows[static_cast<std::size_t>(index)], false);
}

// Right opens a folder, or steps into it when it is already open; Left closes
// it, or jumps to its parent when there is nothing to close.
static void expandTreeCursor(UiRuntime& ui, bool open) {
  if(ui.folderCursor < 0 || ui.folderCursor >= static_cast<int>(ui.sidebarRows.size())) return;
  const SidebarRow row = ui.sidebarRows[static_cast<std::size_t>(ui.folderCursor)];
  if(row.kind != SidebarRow::Kind::Tree || row.tree.kind == ui::TreeRowKind::Note) {
    if(!open) moveTreeCursor(ui, -1);
    return;
  }
  if(open == row.tree.expanded) {
    moveTreeCursor(ui, open ? 1 : -1);
    return;
  }
  if(!row.tree.expandable) return;
  ui.tree.setExpanded(row.tree.folder, open);
}

// A note with no icon still needs something in the icon column, or its title
// would sit where a folder's does and the two would read as one kind of thing.
static void drawNoteIcon(SDL_Renderer* renderer, TextRenderer& text, std::string_view icon, Rect box, SDL_Color color) {
  if(text.drawIcon(icon, box, color)) return;
  // No emoji face anywhere, or no icon set: a drawn page mark rather than the
  // tofu box the missing glyph would otherwise leave behind.
  const float w = 9.0f;
  const float h = 11.0f;
  const float left = std::round(box.x + (box.w - w) / 2.0f);
  const float topY = std::round(box.y + (box.h - h) / 2.0f);
  stroke(renderer, {left, topY, w, h}, color);
  hLine(renderer, left + 2.0f, left + w - 2.0f, topY + 4.0f, color);
  hLine(renderer, left + 2.0f, left + w - 2.0f, topY + 7.0f, color);
}

static void drawSidebar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().sidebarBg);
  ClipGuard clip(renderer, rect);
  ui.sidebarRect = rect;
  buildSidebarRows(ui, rect);
  if(ui.sidebarRows.empty()) {
    drawEmptyMessage(text, "No library", "No folder of notes is open.",
                     {rect.x + 8, rect.y + 20, rect.w - 16, 110}, "Ctrl+,  Settings");
    return;
  }

  const auto& selection = ui.state.selection();
  const ui::TextStyle rowStyle {ui::FontFamily::Sans, false, false, ui::type().ui};
  for(std::size_t i = 0; i < ui.sidebarRows.size(); ++i) {
    const auto& row = ui.sidebarRows[i];
    if(row.rect.y + row.rect.h < rect.y || row.rect.y > rect.y + rect.h) continue;
    const bool hot = hovered(ui, row.rect);
    const float indent = rect.x + 10.0f + static_cast<float>(row.tree.depth) * kSidebarIndent;

    if(row.kind == SidebarRow::Kind::SectionLabel) {
      drawSectionLabel(text, row.label, rect.x + 18, row.rect.y + 12);
      continue;
    }
    if(row.kind == SidebarRow::Kind::Tag) {
      const bool selected = selection.tag == row.tag;
      drawSelection(renderer, row.rect, selected, hot);
      text.draw("#" + ellipsizeToWidth(text, row.tag, static_cast<int>(row.rect.w - 40), rowStyle),
                rect.x + 20, row.rect.y + 4, selected ? theme().accent : theme().dim, rowStyle);
      continue;
    }

    const bool isNote = row.tree.kind == ui::TreeRowKind::Note;
    // A folder is the current context and a note is the open document, so only
    // the note wears the strong selection: two accent bars at once would read
    // as two selections rather than one place and one file.
    const bool selected = isNote && row.tree.noteId == selection.noteId;
    const bool current = !isNote && selection.tag.empty() && selection.folder == row.tree.folder;
    const bool dropTarget = ui.sidebarDropRow && *ui.sidebarDropRow == i;
    if(current) fill(renderer, row.rect, theme().hoverBg);
    drawSelection(renderer, row.rect, selected, hot || dropTarget);
    if(dropTarget) stroke(renderer, row.rect, theme().accent);

    if(row.disclosure.w > 0.0f) {
      drawDisclosure(renderer, row.disclosure, row.tree.expanded,
                     hovered(ui, row.disclosure) ? theme().text : theme().dim);
    }
    const float labelX = indent + 18.0f;
    if(isNote) {
      drawNoteIcon(renderer, text, row.tree.icon, {indent, row.rect.y + 5.0f, 16.0f, 16.0f},
                   selected ? theme().accent : theme().dim);
    }
    const float countW = row.tree.noteCount > 0 && !isNote ? 26.0f : 8.0f;
    text.draw(ellipsizeToWidth(text, row.tree.label, static_cast<int>(row.rect.x + row.rect.w - labelX - countW), rowStyle),
              labelX, row.rect.y + 5.0f, selected || current ? theme().text : (isNote ? theme().muted : theme().text), rowStyle);
    if(!isNote && row.tree.noteCount > 0) {
      const auto count = std::to_string(row.tree.noteCount);
      text.draw(count, row.rect.x + row.rect.w - static_cast<float>(text.width(count, rowStyle)) - 10.0f,
                row.rect.y + 5.0f, current ? theme().accent : theme().dim, rowStyle);
    }
  }
  drawVerticalScrollbar(renderer, rect, ui.sidebarScroll, ui.sidebarMaxScroll);
}

static void drawNotes(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().notesBg);
  ClipGuard clip(renderer, rect);
  Rect search {rect.x + 14, rect.y + 12, rect.w - 28, 34};
  drawSurface(renderer, search, theme().inputBg, ui.focus == FocusArea::Search ? theme().accentDim : theme().hairline);
  ui.searchScopeToggle = {search.x + search.w - 34, search.y + 5, 24, 24};
  const auto searchLabel = ui.searchDraft.empty() ? "Search all notes" : ui.searchDraft;
  if(ui.focus == FocusArea::Search && ui.inputAllSelected && !ui.searchDraft.empty()) {
    fill(renderer, {search.x + 50, search.y + 6, search.w - 88, 22}, theme().selectionBg);
  }
  text.draw("Find", search.x + 12, search.y + 8, ui.focus == FocusArea::Search ? theme().accent : theme().dim);
  text.draw(ellipsizeToWidth(text, searchLabel, static_cast<int>(search.w - 94)), search.x + 52, search.y + 8, ui.searchDraft.empty() ? theme().dim : theme().text);
  fill(renderer, ui.searchScopeToggle, ui.focus == FocusArea::Search ? theme().accentSoft : theme().surface);
  stroke(renderer, ui.searchScopeToggle, ui.focus == FocusArea::Search ? theme().accentDim : theme().hairline);
  text.draw(searchScopeLabel(ui.searchScope), ui.searchScopeToggle.x + 8, ui.searchScopeToggle.y + 4, ui.focus == FocusArea::Search ? theme().accent : theme().muted);

  float y = rect.y + 62;
  if(!ui.searchDraft.empty()) {
    const auto results = ui.state.currentSearchResults();
    if(results.empty()) {
      drawEmptyMessage(text, "Nothing matches", "No note contains \"" + ui.searchDraft + "\".",
                       {rect.x + 8, y - 8, rect.w - 16, 110}, "Esc  clear the search");
      return;
    }
    for(const auto& result : results) {
      if(y >= rect.y + rect.h - 24) break;
      const bool selected = result.id == ui.state.selection().noteId;
      const std::size_t snippetCount = std::max<std::size_t>(result.snippets.size(), result.matchLine.empty() ? 0 : 1);
      const float availableH = rect.y + rect.h - 24.0f - (y - 8.0f);
      const std::size_t maxVisibleSnippets = availableH <= 90.0f ? 0 : static_cast<std::size_t>((availableH - 30.0f) / 60.0f);
      const std::size_t visibleSnippets = std::min<std::size_t>(snippetCount, std::min<std::size_t>(4, maxVisibleSnippets));
      const float rowH = visibleSnippets > 0 ? 30.0f + static_cast<float>(visibleSnippets * 60) : 50.0f;
      Rect row {rect.x + 10, y - 8, rect.w - 20, rowH};
      drawSelection(renderer, row, selected, hovered(ui, row));
      if(!selected && !hovered(ui, row)) hLine(renderer, row.x + 8, row.x + row.w - 8, row.y + row.h, theme().hairline);
      text.draw(ellipsizeToWidth(text, result.title, static_cast<int>(row.w - 28)), rect.x + 20, y, selected ? theme().text : theme().muted);
      if(visibleSnippets > 0) {
        float snippetY = y + 22;
        for(std::size_t i = 0; i < visibleSnippets; ++i) {
          const auto snippet = result.snippets.empty()
            ? library::SearchResult::Snippet {result.beforeLine, result.matchLine, result.afterLine}
            : result.snippets[i];
          text.draw(ellipsizeToWidth(text, snippet.beforeLine, static_cast<int>(row.w - 28)), rect.x + 20, snippetY, theme().dim, false, true);
          text.draw(ellipsizeToWidth(text, snippet.matchLine, static_cast<int>(row.w - 28)), rect.x + 20, snippetY + 20, selected ? theme().accent : theme().text, false, true);
          text.draw(ellipsizeToWidth(text, snippet.afterLine, static_cast<int>(row.w - 28)), rect.x + 20, snippetY + 40, theme().dim, false, true);
          snippetY += 60.0f;
        }
      }
      y += rowH;
    }
    return;
  }

  const auto notes = ui.state.currentNotes();
  if(notes.empty()) {
    // Three different nothings, and the way out of each is different: no
    // library at all, a library with no notes in it, and a notebook or tag
    // that happens to be empty.
    const Rect where {rect.x + 8, y - 8, rect.w - 16, 110};
    if(!ui.state.hasLibrary()) {
      drawEmptyMessage(text, "No library", "Point micronotes at a folder of notes.",
                       where, "Ctrl+,  Settings");
    } else if(ui.state.allNotes().empty()) {
      drawEmptyMessage(text, "No notes yet", "Notes here are plain .md files.",
                       where, "Ctrl+N  write the first one");
    } else if(!ui.state.selection().tag.empty()) {
      drawEmptyMessage(text, "No notes with this tag", "Nothing carries #" + ui.state.selection().tag + " any more.",
                       where, "click the tag again to clear the filter");
    } else {
      drawEmptyMessage(text, "This notebook is empty", "A note made here lands in this folder.",
                       where, "Ctrl+N  new note");
    }
    return;
  }
  for(std::size_t i = 0; i < notes.size() && y < rect.y + rect.h - 24; ++i) {
    const auto& note = notes[i];
    const bool selected = note.id == ui.state.selection().noteId;
    Rect row {rect.x + 10, y - 8, rect.w - 20, 50};
    drawSelection(renderer, row, selected, hovered(ui, row));
    if(!selected && !hovered(ui, row)) hLine(renderer, row.x + 8, row.x + row.w - 8, row.y + row.h, theme().hairline);
    // The same icon column as the tree, so a note looks like itself wherever it
    // is listed.
    drawNoteIcon(renderer, text, note.icon, {rect.x + 18, y, 18, 18}, selected ? theme().accent : theme().dim);
    text.draw(ellipsizeToWidth(text, note.title, static_cast<int>(row.w - 52)), rect.x + 42, y, selected ? theme().text : theme().muted);
    if(!note.tags.empty()) {
      const auto tagLabel = "#" + ellipsize(note.tags.front(), 18);
      const float chipW = std::min(static_cast<float>(text.width(tagLabel) + 22), row.w - 58);
      Rect chip {rect.x + 42, y + 22, chipW, 22};
      fill(renderer, chip, selected ? theme().accentSoft : theme().chipBg);
      stroke(renderer, chip, selected ? theme().accentDim : theme().hairline);
      text.draw(tagLabel, chip.x + 10, chip.y + std::max(2.0f, (chip.h - static_cast<float>(text.lineHeight())) / 2.0f), selected ? theme().accent : theme().dim);
    }
    y += 50;
  }
}

static editor::MeasureText editorMeasure(TextRenderer& text) {
  return [&text](std::string_view value) {
    return text.width(value, false, true);
  };
}

static const std::vector<editor::SoftWrapRow>& editorRows(TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect writing = editorWritingRect(rect);
  const int wrapWidth = static_cast<int>(std::max(1.0f, writing.w - 20.0f));
  const auto& source = ui.editor.text();
  if(ui.cachedEditorRowsWidth != wrapWidth || ui.cachedEditorRowsSource != source) {
    ui.cachedEditorRowsWidth = wrapWidth;
    ui.cachedEditorRowsSource = source;
    ui.cachedEditorRows = editor::softWrap(ui.cachedEditorRowsSource, wrapWidth, editorMeasure(text));
  }
  return ui.cachedEditorRows;
}

static int editorMaxScroll(TextRenderer& text, UiRuntime& ui, Rect rect) {
  Rect writing = editorWritingRect(rect);
  const int lineHeight = text.lineHeight();
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  ui.editorVisibleRows = maxLines;
  const int lineCount = static_cast<int>(editorRows(text, ui, rect).size());
  return std::max(0, lineCount - maxLines);
}

static bool scrollbarHit(Rect viewport, int scroll, int maxScroll, float x, float y) {
  if(maxScroll <= 0) return false;
  return contains(scrollbarHitRect(scrollbarThumb(viewport, scroll, maxScroll)), x, y);
}

static CursorKind classifyCursor(TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(ui.resizingSidebar || ui.resizingNotes) return CursorKind::ResizeHorizontal;
  if(ui.scrollDragTarget != ScrollDragTarget::None) return CursorKind::ResizeVertical;

  const float x = ui.mouseX;
  const float y = ui.mouseY;
  const AppLayout layout = computeLayout(ui.state.shell(), width, height);
  if(isResizeGutter(layout, x, y)) return CursorKind::ResizeHorizontal;
  if(contains(layout.crumbs, x, y)) {
    if(contains(ui.favoriteButton, x, y)) return CursorKind::Pointer;
    for(const auto& [rect, folder] : ui.crumbs) {
      (void)folder;
      if(contains(rect, x, y)) return CursorKind::Pointer;
    }
  }

  if(ui.overlays.active()) return CursorKind::Pointer;

  if(contains(layout.sidebar, x, y)) {
    if(sidebarRowAt(ui, layout.sidebar, x, y)) return CursorKind::Pointer;
    return CursorKind::Default;
  }

  if(contains(layout.notes, x, y)) {
    const Rect search = searchBoxRect(layout.notes);
    if(contains(search, x, y)) {
      const Rect scopeToggle {search.x + search.w - 34.0f, search.y + 5.0f, 24.0f, 24.0f};
      return contains(scopeToggle, x, y) ? CursorKind::Pointer : CursorKind::Text;
    }
    return noteRowAt(ui, layout.notes, x, y) ? CursorKind::Pointer : CursorKind::Default;
  }

  if(!contains(layout.content, x, y)) return CursorKind::Default;

  if(ui.state.shell().paneMode == ui::PaneMode::Live) {
    if(scrollbarHit(ui.livePage.pageRect(), ui.livePage.scroll(), ui.livePage.maxScroll(), x, y)) return CursorKind::Pointer;
    if(!ui.livePage.linkAt(x, y).empty()) return CursorKind::Pointer;
    if(ui.livePage.gutterAt(x, y) || !ui.livePage.toolbarAt(x, y).empty()) return CursorKind::Pointer;
    if(ui.livePage.foldAt(x, y) || ui.livePage.copyButtonAt(x, y)) return CursorKind::Pointer;
    if(ui.livePage.checkboxAt(x, y)) return CursorKind::Pointer;
    return contains(ui.livePage.pageRect(), x, y) ? CursorKind::Text : CursorKind::Default;
  }

  Rect editorRect = layout.content;
  Rect viewerRect = layout.content;
  bool hasEditor = false;
  bool hasViewer = false;
  if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
    hasEditor = true;
  } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
    hasViewer = true;
  } else {
    hasEditor = true;
    hasViewer = true;
    editorRect.w = layout.content.w / 2.0f;
    viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
  }

  if(hasEditor && contains(editorRect, x, y)) {
    const Rect writing = editorWritingRect(editorRect);
    if(scrollbarHit(writing, ui.editorScroll, editorMaxScroll(text, ui, editorRect), x, y)) {
      return CursorKind::Pointer;
    }
    if(contains(writing, x, y)) return CursorKind::Text;
  }

  if(hasViewer && contains(viewerRect, x, y)) {
    const Rect page = viewerPageRect(viewerRect);
    if(scrollbarHit(page, ui.viewerScroll, viewerMaxScroll(text, ui, viewerRect), x, y)) {
      return CursorKind::Pointer;
    }
    for(const auto& link : ui.linkRegions) {
      if(contains(link.rect, x, y)) return CursorKind::Pointer;
    }
  }

  return CursorKind::Default;
}

static std::size_t editorIndexAtPoint(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  const int lineHeight = text.lineHeight();
  const auto& rows = editorRows(text, ui, rect);
  const Rect writing = editorWritingRect(rect);
  const int visibleLine = std::max(0, static_cast<int>((y - (writing.y + 12)) / static_cast<float>(lineHeight)));
  const int rowIndex = std::clamp(ui.editorScroll + visibleLine, 0, std::max(0, static_cast<int>(rows.size()) - 1));
  return editor::offsetForRowX(rows[static_cast<std::size_t>(rowIndex)], x - (writing.x + 12), editorMeasure(text));
}

static void placeEditorCursor(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  ui.editor.moveCursor(editorIndexAtPoint(text, ui, rect, x, y));
  ui.revealEditorCursor = true;
}

static void selectWordAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  std::size_t cursor = std::min(ui.editor.cursor(), value.size());
  if(cursor > 0 && (cursor == value.size() || !std::isalnum(static_cast<unsigned char>(value[cursor])))) --cursor;
  std::size_t start = cursor;
  std::size_t end = cursor;
  while(start > 0 && (std::isalnum(static_cast<unsigned char>(value[start - 1])) || value[start - 1] == '_')) --start;
  while(end < value.size() && (std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_')) ++end;
  ui.editor.selectRange(start, end);
}

static void selectLineAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  const auto cursor = std::min(ui.editor.cursor(), value.size());
  const auto lineStart = value.rfind('\n', cursor == 0 ? 0 : cursor - 1);
  const auto lineEnd = value.find('\n', cursor);
  const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
  const std::size_t end = lineEnd == std::string::npos ? value.size() : lineEnd;
  ui.editor.selectRange(start, end);
}

static void drawEditor(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().editorBg);
  Rect writing {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  drawSurface(renderer, writing, theme().pageSurface, ui.focus == FocusArea::Editor ? theme().accentDim : theme().hairline);
  const int lineHeight = text.lineHeight();
  const auto& rows = editorRows(text, ui, rect);
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  const int cursorRow = editor::rowForOffset(rows, ui.editor.cursor());
  if(ui.revealEditorCursor) {
    if(cursorRow < ui.editorScroll) ui.editorScroll = cursorRow;
    if(cursorRow >= ui.editorScroll + maxLines) ui.editorScroll = cursorRow - maxLines + 1;
  }
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - maxLines);
  ui.editorScroll = std::clamp(ui.editorScroll, 0, maxScroll);
  ui.revealEditorCursor = false;
  {
    ClipGuard clip(renderer, {writing.x + 1, writing.y + 1, writing.w - 2, writing.h - 2});
    float y = writing.y + 12;
    for(int i = ui.editorScroll; i < static_cast<int>(rows.size()) && y < writing.y + writing.h - 12; ++i) {
      const auto& row = rows[static_cast<std::size_t>(i)];
      const auto& line = row.text;
      if(ui.editor.hasSelection()) {
        const auto selStart = std::max(ui.editor.selectionStart(), row.start);
        const auto selEnd = std::min(ui.editor.selectionEnd(), row.end);
        if(selStart < selEnd) {
          const auto before = std::string_view(line.data(), selStart - row.start);
          const auto selected = std::string_view(line.data() + (selStart - row.start), selEnd - selStart);
          const float sx = writing.x + 12 + static_cast<float>(text.width(before, false, true));
          const float sw = static_cast<float>(text.width(selected, false, true));
          fill(renderer, {sx, y - 2, std::min(sw, writing.x + writing.w - 8 - sx), static_cast<float>(lineHeight)}, theme().selectionBg);
        }
      }
      drawFindHighlights(renderer, text, ui, line, writing, y);
      text.draw(line.empty() ? " " : line, writing.x + 12, y, theme().text, false, true);
      y += lineHeight;
    }
    if(ui.focus == FocusArea::Editor && cursorRow >= ui.editorScroll && cursorRow < ui.editorScroll + maxLines) {
      std::string prefix;
      if(cursorRow >= 0 && cursorRow < static_cast<int>(rows.size())) {
        const auto& row = rows[static_cast<std::size_t>(cursorRow)];
        const auto cursorInRow = ui.editor.cursor() <= row.end ? ui.editor.cursor() - row.start : row.text.size();
        prefix = row.text.substr(0, std::min<std::size_t>(row.text.size(), cursorInRow));
      }
      const float cursorX = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
      const float cursorY = writing.y + 12 + static_cast<float>((cursorRow - ui.editorScroll) * lineHeight);
      fill(renderer, {std::min(cursorX, writing.x + writing.w - 8), cursorY, 2, static_cast<float>(lineHeight - 2)}, theme().accent);
    }
    if(ui.editor.text().empty()) text.draw("Start typing...", writing.x + 12, writing.y + 12, theme().muted);
  }
  drawVerticalScrollbar(renderer, writing, ui.editorScroll, maxScroll);
}

static void drawViewer(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, theme().viewerBg);
  Rect page {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  drawSurface(renderer, page, theme().pageSurface, ui.focus == FocusArea::Viewer ? theme().accentDim : theme().hairline);
  attachments::AttachmentService attachmentService;
  const auto& doc = previewDocument(ui);
  const float contentTop = page.y + 14.0f;
  float contentLeft = 0.0f;
  float contentWidth = 0.0f;
  contentColumn(page, contentLeft, contentWidth);
  float measureY = contentTop;
  int orderedIndex = 1;
  int footnoteIndex = 1;
  ui.viewerAnchors.clear();
  for(const auto& block : doc.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const bool ordered = block.type == markdown::BlockType::OrderedItem;
    const bool rule = block.type == markdown::BlockType::HorizontalRule;
    const bool table = block.type == markdown::BlockType::Table;
    const bool footnote = block.type == markdown::BlockType::Footnote;
    const bool html = block.type == markdown::BlockType::Html;
    const bool blankLine = block.type == markdown::BlockType::BlankLine;
    if(!ordered) orderedIndex = 1;
    if(heading) {
      const auto anchor = anchorFor(blockText(block));
      if(!anchor.empty()) ui.viewerAnchors[anchor] = static_cast<int>(std::max(0.0f, measureY - contentTop));
    } else if(footnote && !block.footnoteLabel.empty()) {
      ui.viewerAnchors["fn-" + block.footnoteLabel] = static_cast<int>(std::max(0.0f, measureY - contentTop));
      ui.viewerAnchors["fn-" + std::to_string(footnoteIndex++)] = static_cast<int>(std::max(0.0f, measureY - contentTop));
    }
    const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
    const float markerW = listMarkerWidth(block);
    const float quoteW = block.type == markdown::BlockType::Quote ? 16.0f : 0.0f;
    const ui::TextStyle blockStyle = blockTextStyle(block);
    if(blankLine) {
      measureY += static_cast<float>(text.lineHeight());
    } else if(rule) {
      measureY += 22.0f;
    } else if(table) {
      measureY += tableHeight(text, block, contentWidth - indentW) + 12.0f;
    } else if(code) {
      const auto lines = codeBlockLines(block);
      measureY += static_cast<float>(std::max<std::size_t>(1, lines.size()) * lineStepFor(text, blockStyle, 1.5f)) + 18.0f;
    } else {
      const auto value = blockText(block);
      if(!value.empty()) {
        const auto runs = inlineRuns(block, theme().text);
        const int lineStep = blockLineStep(text, block);
        const float chromeW = markerW + quoteW + indentW + admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
        measureY += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(contentWidth - chromeW), blockStyle.size) * lineStep) + blockBottomSpacing(block, heading, html);
      }
      for(const auto& image : blockImages(block)) {
        float imageW = 0;
        float imageH = 0;
        float renderedH = imagePlaceholderHeight(text, imagePlaceholder(image, ui, attachmentService), contentWidth);
        if(!isRemoteTarget(image.target) && ui.state.hasLibrary()) {
          try {
            const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
            SDL_Texture* texture = images.load(path, imageW, imageH);
            if(texture && imageW > 0 && imageH > 0) {
              const float maxW = std::max(40.0f, std::min(contentWidth, 720.0f));
              const float maxH = std::max(40.0f, page.h * 0.55f);
              const float scale = std::min(1.0f, maxW / imageW);
              renderedH = imageH * std::min(scale, maxH / imageH) + 14.0f;
            }
          } catch(const std::exception&) {
          }
        }
        measureY += renderedH;
      }
    }
    if(ordered) ++orderedIndex;
  }
  const int maxScroll = std::max(0, static_cast<int>(std::ceil(measureY - contentTop - page.h + 24.0f)));
  ui.viewerScroll = std::clamp(ui.viewerScroll, 0, maxScroll);
  {
    ClipGuard clip(renderer, {page.x + 1, page.y + 1, page.w - 2, page.h - 2});
    float y = contentTop - static_cast<float>(ui.viewerScroll);
    orderedIndex = 1;
    for(const auto& block : doc.blocks) {
      const bool heading = block.type == markdown::BlockType::Heading;
      const bool code = block.type == markdown::BlockType::Code;
      const bool ordered = block.type == markdown::BlockType::OrderedItem;
      const bool unordered = block.type == markdown::BlockType::UnorderedItem;
      const bool quote = block.type == markdown::BlockType::Quote;
      const bool rule = block.type == markdown::BlockType::HorizontalRule;
      const bool table = block.type == markdown::BlockType::Table;
      const bool admonition = block.type == markdown::BlockType::Admonition;
      const bool footnote = block.type == markdown::BlockType::Footnote;
      const bool blankLine = block.type == markdown::BlockType::BlankLine;
      if(!ordered) orderedIndex = 1;
      const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
      const float markerW = listMarkerWidth(block);
      const float quoteW = quote ? 16.0f : 0.0f;
      const float extraW = admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
      const float textX = contentLeft + indentW + markerW + quoteW + extraW;
      const ui::TextStyle blockStyle = blockTextStyle(block);
      if(blankLine) {
        y += static_cast<float>(text.lineHeight());
      } else if(rule) {
        if(y + 12.0f >= page.y && y <= page.y + page.h) {
          hLine(renderer, contentLeft + indentW, contentLeft + contentWidth, y + 8.0f, theme().divider);
        }
        y += 22.0f;
      } else if(table) {
        const float blockH = tableHeight(text, block, contentWidth - indentW);
        if(y + blockH >= page.y && y <= page.y + page.h) {
          drawTable(renderer, text, ui.linkRegions, block, {contentLeft + indentW, y, contentWidth - indentW, blockH});
        }
        y += blockH + 12.0f;
      } else if(code) {
        const auto lines = codeBlockLines(block);
        const float blockH = static_cast<float>(std::max<std::size_t>(1, lines.size()) * lineStepFor(text, blockStyle, 1.5f)) + 10.0f;
        Rect codeRect {contentLeft + indentW, y - 6.0f, contentWidth - indentW, blockH};
        if(codeRect.y + codeRect.h >= page.y && codeRect.y <= page.y + page.h) {
          drawSurface(renderer, codeRect, theme().codeBg, theme().hairline);
          float codeY = y;
          for(const auto& codeLine : lines) {
            text.draw(ellipsizeToWidth(text, codeLine, static_cast<int>(codeRect.w - 20.0f), false, true), codeRect.x + 10.0f, codeY, theme().text, blockStyle);
            codeY += static_cast<float>(lineStepFor(text, blockStyle, 1.5f));
          }
        }
        y += blockH + 8.0f;
      } else {
        const auto value = blockText(block);
        if(!value.empty()) {
          const auto runs = inlineRuns(block, theme().text);
          const int lineStep = blockLineStep(text, block);
          const float blockWidth = contentWidth - indentW - markerW - quoteW - extraW;
          const float blockH = static_cast<float>(measureInlineLines(text, runs, static_cast<int>(blockWidth), blockStyle.size) * lineStep);
          if(quote && y + blockH >= page.y && y <= page.y + page.h) {
            fill(renderer, {contentLeft + indentW, y - 2.0f, 3.0f, blockH + 2.0f}, theme().divider);
          }
          if(admonition && y + blockH >= page.y && y <= page.y + page.h) {
            // The same palette the live surface uses, so a callout does not
            // change colour when the note is read instead of edited.
            const ui::CalloutStyle callStyle = ui::calloutStyle(block.admonitionType);
            Rect callout {contentLeft + indentW, y - 7.0f, contentWidth - indentW, blockH + 12.0f};
            drawSurface(renderer, callout, callStyle.surface, callStyle.surface);
            fill(renderer, {callout.x, callout.y, 3.0f, callout.h}, callStyle.accent);
            const auto label = block.admonitionType.empty() ? "note" : block.admonitionType;
            text.draw(label, callout.x + 10.0f, y, callStyle.accent, false, false, true);
          }
          if(footnote && y + blockH >= page.y && y <= page.y + page.h) {
            const auto label = block.footnoteLabel.empty() ? "*" : block.footnoteLabel;
            text.draw("[" + label + "]", contentLeft + indentW, y, theme().accent);
          }
          if(ordered && y + blockH >= page.y && y <= page.y + page.h) {
            const int number = block.orderedNumber > 0 ? block.orderedNumber : orderedIndex;
            text.draw(std::to_string(number) + ".", contentLeft + indentW, y, theme().muted);
          } else if(unordered && y + blockH >= page.y && y <= page.y + page.h) {
            if(block.task) {
              Rect box {contentLeft + indentW, y + 3.0f, 12.0f, 12.0f};
              stroke(renderer, box, block.taskChecked ? theme().accent : theme().muted);
              if(block.taskChecked) {
                hLine(renderer, box.x + 2.0f, box.x + 5.0f, box.y + 7.0f, theme().accent);
                hLine(renderer, box.x + 5.0f, box.x + 10.0f, box.y + 3.0f, theme().accent);
              }
            } else {
              text.draw("\u2022", contentLeft + indentW, y, theme().muted);
            }
          }
          if(y + blockH >= page.y && y <= page.y + page.h) {
            drawInlineRuns(renderer, text, &ui.linkRegions, runs, textX, y, static_cast<int>(blockWidth), lineStep, blockStyle.size);
          }
          y += blockH + blockBottomSpacing(block, heading, false);
        }
        for(const auto& image : blockImages(block)) {
          float imageW = 0;
          float imageH = 0;
          SDL_Texture* texture = nullptr;
          std::string placeholder = imagePlaceholder(image, ui, attachmentService);
          if(!isRemoteTarget(image.target) && ui.state.hasLibrary()) {
            try {
              const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
              if(attachmentService.isSupportedImage(path)) {
                texture = images.load(path, imageW, imageH);
              }
            } catch(const std::exception&) {
            }
          }
          if(texture && imageW > 0 && imageH > 0) {
            const float maxW = std::max(40.0f, std::min(contentWidth, 720.0f));
            const float maxH = std::max(40.0f, page.h * 0.55f);
            const float scale = std::min(1.0f, maxW / imageW);
            const float finalScale = std::min(scale, maxH / imageH);
            SDL_FRect dst {contentLeft, std::round(y), std::round(imageW * finalScale), std::round(imageH * finalScale)};
            if(dst.y + dst.h >= page.y && dst.y <= page.y + page.h) {
              SDL_RenderTexture(renderer, texture, nullptr, &dst);
              ui.linkRegions.push_back({{dst.x, dst.y, dst.w, dst.h}, image.target});
            }
            y += dst.h + 14.0f;
          } else {
            const auto lines = wrapText(text, placeholder, static_cast<int>(contentWidth), false, true);
            float placeholderY = y;
            const bool clickablePlaceholder = isRemoteTarget(image.target);
            for(const auto& line : lines) {
              if(placeholderY + text.lineHeight() >= page.y && placeholderY <= page.y + page.h) {
                const auto lineW = static_cast<float>(text.width(line, false, true));
                text.draw(line, contentLeft, placeholderY, clickablePlaceholder ? theme().accent : theme().dim, false, true);
                if(clickablePlaceholder && lineW > 0.0f) {
                  ui.linkRegions.push_back({{contentLeft, placeholderY, lineW, static_cast<float>(text.lineHeight())}, image.target});
                  hLine(renderer, contentLeft, contentLeft + lineW, placeholderY + static_cast<float>(text.lineHeight() - 2), theme().accentDim);
                }
              }
              placeholderY += static_cast<float>(text.lineHeight() + 2);
            }
            y = placeholderY + 8.0f;
          }
        }
      }
      if(ordered) ++orderedIndex;
    }
    if(doc.blocks.empty()) {
      drawEmptyMessage(text, "Nothing to read yet", "This note has no text in it.", page,
                       ui.state.shell().paneMode == ui::PaneMode::Split ? "type on the left" : "Ctrl+1  go back and write");
    }
  }
  drawVerticalScrollbar(renderer, page, ui.viewerScroll, maxScroll);
}

// A block the live scanner does not model is parsed on its own and rendered by
// the md4c path, so tables and raw HTML look the same everywhere.
static const markdown::Document& complexDocument(UiRuntime& ui, const doc::SourceBlock& block) {
  const auto& source = ui.editor.text();
  const std::size_t start = std::min(block.start, source.size());
  const std::size_t end = std::min(block.end, source.size());
  std::string key = source.substr(start, end - start);
  auto found = ui.complexCache.find(key);
  if(found == ui.complexCache.end()) {
    if(ui.complexCache.size() > 64) ui.complexCache.clear();
    found = ui.complexCache.emplace(key, ui.parser.parse(key)).first;
  }
  return found->second;
}

// md4c renders a few constructs (a lone footnote definition, say) to nothing at
// all. Falling back to the source keeps such a block visible and editable.
static bool complexRendersNothing(const markdown::Document& document) {
  for(const auto& item : document.blocks) {
    if(item.type == markdown::BlockType::Table) return false;
    if(!blockText(item).empty()) return false;
  }
  return true;
}

static std::vector<std::string> complexSourceLines(UiRuntime& ui, const doc::SourceBlock& block) {
  const auto& source = ui.editor.text();
  const std::size_t start = std::min(block.start, source.size());
  const std::size_t end = std::min(block.end, source.size());
  return splitLines(std::string_view(source).substr(start, end - start));
}

static float measureComplexBlock(TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block, float width) {
  const auto& document = complexDocument(ui, block);
  if(complexRendersNothing(document)) {
    ui::TextStyle mono;
    mono.family = ui::FontFamily::Mono;
    mono.size = ui::type().mono;
    return static_cast<float>(complexSourceLines(ui, block).size() * lineStepFor(text, mono, 1.5f)) + 20.0f;
  }
  float height = 10.0f;
  for(const auto& item : document.blocks) {
    if(item.type == markdown::BlockType::Table) {
      height += tableHeight(text, item, width) + 12.0f;
    } else if(item.type == markdown::BlockType::BlankLine) {
      height += static_cast<float>(text.lineHeight());
    } else {
      const auto runs = inlineRuns(item, theme().text);
      const auto style = blockTextStyle(item);
      height += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(width), style.size) * blockLineStep(text, item)) + 6.0f;
    }
  }
  return height + 10.0f;
}

static void drawComplexBlock(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block, Rect rect) {
  const auto& document = complexDocument(ui, block);
  if(complexRendersNothing(document)) {
    ui::TextStyle mono;
    mono.family = ui::FontFamily::Mono;
    mono.size = ui::type().mono;
    const int step = lineStepFor(text, mono, 1.5f);
    float y = rect.y + 10.0f;
    for(const auto& line : complexSourceLines(ui, block)) {
      text.draw(ellipsizeToWidth(text, line, static_cast<int>(rect.w), mono), rect.x, y, theme().muted, mono);
      y += static_cast<float>(step);
    }
    return;
  }
  float y = rect.y + 10.0f;
  for(const auto& item : document.blocks) {
    if(item.type == markdown::BlockType::Table) {
      const float height = tableHeight(text, item, rect.w);
      drawTable(renderer, text, ui.linkRegions, item, {rect.x, y, rect.w, height});
      y += height + 12.0f;
    } else if(item.type == markdown::BlockType::BlankLine) {
      y += static_cast<float>(text.lineHeight());
    } else {
      const auto runs = inlineRuns(item, theme().text);
      const auto style = blockTextStyle(item);
      const int step = blockLineStep(text, item);
      drawInlineRuns(renderer, text, &ui.linkRegions, runs, rect.x, y, static_cast<int>(rect.w), step, style.size);
      y += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(rect.w), style.size) * step) + 6.0f;
    }
  }
}

static void drawLive(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  PageViewHooks hooks;
  hooks.measureComplex = [&text, &ui](const doc::SourceBlock& block, float width) {
    return measureComplexBlock(text, ui, block, width);
  };
  hooks.drawComplex = [renderer, &text, &ui](const doc::SourceBlock& block, Rect area) {
    drawComplexBlock(renderer, text, ui, block, area);
  };
  ui.livePage.setHooks(std::move(hooks));

  PageFolds folds;
  const std::string noteId = ui.state.selection().noteId;
  folds.collapsed = [&ui, noteId](const doc::SourceBlock& block) {
    // Building a key means building a string, so a note with nothing folded
    // never pays for one.
    if(!ui.folds.anyFolded(noteId)) return false;
    return ui.folds.folded(noteId, doc::foldKey(ui.editor.text(), block));
  };
  folds.expand = [&ui, noteId](const doc::SourceBlock& block) {
    ui.folds.unfold(noteId, doc::foldKey(ui.editor.text(), block));
  };
  ui.livePage.setFolds(std::move(folds));
  ui.livePage.setPointer(ui.mouseX, ui.mouseY);
  ui.livePage.setBlockSelection({ui.blockSelectActive, ui.blockSelectAnchor, ui.blockSelectFocus});
  ui.livePage.setDropOffset(ui.draggingBlock ? ui.blockDropOffset : std::nullopt);
  ui.livePage.setSelecting(ui.selectingEditorText);
  ui.livePage.layout(text, ui.editor.text(), ui.editor.cursor(), rect);

  // Leaving a block that was dropped to raw text hands it back to md4c.
  if(const auto raw = ui.livePage.rawOffset()) {
    const auto& blocks = ui.livePage.document().blocks();
    const auto index = doc::blockIndexAt(blocks, std::min(*raw, ui.editor.text().size()));
    const auto& block = blocks[index];
    const auto cursor = ui.editor.cursor();
    if(cursor < block.start || cursor >= block.end) {
      ui.livePage.setRawOffset(std::nullopt);
      ui.livePage.layout(text, ui.editor.text(), cursor, rect);
    }
  }

  if(ui.revealEditorCursor) {
    ui.livePage.revealCaret(ui.editor.cursor());
    ui.revealEditorCursor = false;
  }
  PageSelection selection;
  if(ui.editor.hasSelection()) {
    selection.start = ui.editor.selectionStart();
    selection.end = ui.editor.selectionEnd();
  }
  ui.livePage.draw(renderer, text, ui.editor.cursor(), selection, ui.focus == FocusArea::Editor, ui.findDraft);
  for(const auto& link : ui.livePage.links()) ui.linkRegions.push_back({link.rect, link.target});
  if(ui.editor.text().empty()) {
    // On the content column rather than the page edge, so the prompt sits
    // exactly where the first character typed will appear.
    const Rect column = ui.livePage.columnRect();
    const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().body};
    text.draw("Write something. Press / for a block.", column.x, column.y, theme().dim, style);
  }
}

static void drawStatus(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  // Three anchors and the way to the rest. The line used to name a dozen keys
  // and be truncated before it finished; every one of them is in F1 now, which
  // can hold them all and be searched.
  std::string help = std::string(paneModeName(ui.state.shell().paneMode)) +
    "   Ctrl+P Go to note   Ctrl+Shift+P Commands   F1 Shortcuts";
  if(ui.focus == FocusArea::Search) help = "Search all: " + ui.searchDraft + "    Enter open  Esc clear";
  if(ui.focus == FocusArea::Find) help = "Find in note: " + ui.findDraft + "    Esc close";
  fill(renderer, {rect.x + 12, rect.y + 7, 6, 6}, ui.editor.dirty() ? theme().warn : theme().accent);
  text.draw(ellipsize(help, 100), rect.x + 28, rect.y + 6, theme().muted);
  if(!ui.status.empty()) {
    const auto message = ellipsize(ui.status, 72);
    Rect pill {std::max(rect.x + 12, rect.x + rect.w - static_cast<float>(text.width(message)) - 30), rect.y + 3, static_cast<float>(text.width(message)) + 18, 22};
    fill(renderer, pill, theme().surface);
    stroke(renderer, pill, theme().hairline);
    text.draw(message, pill.x + 9, rect.y + 6, theme().muted);
  }
}

// The trail of folders down to the open note. Clicking a crumb selects that
// folder, which is the shortest way back up from anywhere in the library.
static void drawBreadcrumbs(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui.crumbs.clear();
  ui.favoriteButton = {};
  fill(renderer, rect, theme().editorBg);
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h - 1.0f, theme().hairline);
  const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().small};
  const auto note = ui.state.findNote(ui.state.selection().noteId);

  const float baseline = rect.y + std::max(4.0f, (rect.h - static_cast<float>(text.lineHeight(style))) / 2.0f);
  float x = rect.x + 20.0f;
  const float limit = rect.x + rect.w - 44.0f;

  // Every crumb down to the note's own folder, root first.
  std::vector<std::filesystem::path> trail {{}};
  if(note) {
    std::filesystem::path walk;
    for(const auto& part : note->path.lexically_relative(ui.state.libraryRoot()).parent_path()) {
      walk /= part;
      trail.push_back(walk);
    }
  }
  for(std::size_t i = 0; i < trail.size() && x < limit; ++i) {
    const auto label = trail[i].empty() ? ui.state.libraryRoot().filename().generic_string()
                                        : trail[i].filename().generic_string();
    const float w = static_cast<float>(text.width(label, style));
    const Rect hit {x - 4.0f, rect.y + 4.0f, w + 8.0f, rect.h - 8.0f};
    const bool hot = hovered(ui, hit);
    if(hot) fill(renderer, hit, theme().hoverBg);
    text.draw(label, x, baseline, hot ? theme().text : theme().muted, style);
    ui.crumbs.emplace_back(hit, trail[i]);
    x += w + 8.0f;
    text.draw("/", x, baseline, theme().dim, style);
    x += static_cast<float>(text.width("/", style)) + 8.0f;
  }
  if(note && x < limit) {
    drawNoteIcon(renderer, text, note->icon, {x, rect.y + 6.0f, 16.0f, 16.0f}, theme().dim);
    x += 20.0f;
    text.draw(ellipsizeToWidth(text, note->title, static_cast<int>(limit - x), style), x, baseline, theme().text, style);
  }

  if(note) {
    // A filled star reads as "kept"; the outline is an offer.
    ui.favoriteButton = {rect.x + rect.w - 34.0f, rect.y + 4.0f, 26.0f, rect.h - 8.0f};
    const bool pinned = ui.state.favorite(note->id);
    if(hovered(ui, ui.favoriteButton)) fill(renderer, ui.favoriteButton, theme().hoverBg);
    text.draw(pinned ? "\xe2\x98\x85" : "\xe2\x98\x86", ui.favoriteButton.x + 6.0f, baseline,
              pinned ? theme().accent : theme().dim, style);
  }
}

static void drawApp(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, int width, int height) {
  SDL_SetRenderDrawColor(renderer, theme().appBg.r, theme().appBg.g, theme().appBg.b, theme().appBg.a);
  SDL_RenderClear(renderer);

  const AppLayout layout = computeLayout(ui.state.shell(), width, height);
  ui.linkRegions.clear();
  ui.buttonRegions.clear();

  drawSidebar(renderer, text, ui, layout.sidebar);
  drawNotes(renderer, text, ui, layout.notes);
  fill(renderer, {layout.sidebar.x + layout.sidebar.w, 0, 1, layout.sidebar.h}, theme().hairline);
  fill(renderer, {layout.notes.x + layout.notes.w, 0, 1, layout.notes.h}, theme().hairline);
  if(!ui.state.hasLibrary()) {
    fill(renderer, layout.content, theme().editorBg);
    // The one screen someone can arrive at knowing nothing, so it says what
    // the app is for before it says which key to press.
    drawEmptyMessage(text, "Open a folder of notes",
                     "micronotes reads and writes plain Markdown files in one local folder. Nothing leaves your disk.",
                     {layout.content.x + 18, layout.content.y + 40, layout.content.w - 36, 130},
                     "Ctrl+,  Settings          or start with  --library <path>");
  } else if(ui.state.selection().noteId.empty()) {
    ui.crumbs.clear();
    ui.favoriteButton = {};
    fill(renderer, layout.content, theme().editorBg);
    drawEmptyMessage(text, "Nothing open", "Pick a note from the sidebar, or start a new one.",
                     {layout.content.x + 18, layout.content.y + 40, layout.content.w - 36, 130},
                     "Ctrl+P  go to note          Ctrl+N  new note          F1  every shortcut");
  } else {
    // The breadcrumb belongs to the note rather than to a pane, so it sits
    // above whichever pane is showing it.
    const Rect content = layout.content;
    drawBreadcrumbs(renderer, text, ui, layout.crumbs);
    if(ui.state.shell().paneMode == ui::PaneMode::Live) {
      drawLive(renderer, text, ui, content);
    } else if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
      drawEditor(renderer, text, ui, content);
    } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
      drawViewer(renderer, text, images, ui, content);
    } else {
      const float split = content.w / 2.0f;
      drawEditor(renderer, text, ui, {content.x, content.y, split, content.h});
      fill(renderer, {content.x + split, content.y, 1, content.h}, theme().hairline);
      drawViewer(renderer, text, images, ui, {content.x + split, content.y, content.w - split, content.h});
    }
  }
  fill(renderer, layout.status, theme().statusBg);
  fill(renderer, {layout.status.x, layout.status.y, layout.status.w, 1}, theme().hairline);
  drawStatus(renderer, text, ui, layout.status);
  ui.overlays.draw(renderer, text, width, height);
  SDL_RenderPresent(renderer);
}

static int captureFrame(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, const ApplicationOptions& options) {
  // Pump enough events for the compositor to map and size the window before the
  // pixels are read back; an unmapped window reads back blank.
  for(int i = 0; i < 60; ++i) {
    SDL_Event event;
    while(SDL_PollEvent(&event)) {}
    int width = options.windowWidth;
    int height = options.windowHeight;
    SDL_GetWindowSize(renderer ? SDL_GetRenderWindow(renderer) : nullptr, &width, &height);
    drawApp(renderer, text, images, ui, width, height);
    SDL_Delay(8);
  }

  SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr);
  if(!frame) {
    std::cerr << "SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
    return 1;
  }

  const auto path = options.screenshotPath;
  bool saved = false;
#if MICRONOTES_HAS_SDL3_IMAGE
  saved = IMG_SavePNG(frame, path.c_str());
#endif
  if(!saved) saved = SDL_SaveBMP(frame, path.c_str());
  SDL_DestroySurface(frame);
  if(!saved) {
    std::cerr << "Saving screenshot failed: " << SDL_GetError() << "\n";
    return 1;
  }
  std::cout << "wrote " << path.string() << "\n";
  return 0;
}

static void openNoteMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "note-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 220.0f;
  const bool hasNote = !ui.state.selection().noteId.empty();
  overlay.items = {
    {"new", "New note", "", "Ctrl+N", true, false},
    {"rename", "Rename", "", "", hasNote, false},
    {"icon", "Set icon", "", "", hasNote, false},
    {"tags", "Edit tags", "", "Ctrl+T", hasNote, false},
    {"favorite", "Toggle favorite", "", "", hasNote, false},
    {"move", "Move to notebook", "", "", hasNote, false},
    {"delete", "Delete", "", "", hasNote, true},
  };
  ui.overlays.open(std::move(overlay));
}

static void openFolderMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "folder-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 220.0f;
  const bool hasFolder = !ui.state.selection().folder.empty();
  overlay.items = {
    {"new-folder", "New notebook", "", "", true, false},
    {"new-note", "New note here", "", "", hasFolder, false},
    {"rename", "Rename", "", "", hasFolder, false},
    {"delete", "Delete", "", "", hasFolder, true},
  };
  ui.overlays.open(std::move(overlay));
}

static std::vector<ui::OverlayItem> blockKindItems() {
  std::vector<ui::OverlayItem> items;
  for(const auto& entry : kBlockKinds) items.push_back({entry.id, entry.label, entry.detail, "", true, false});
  return items;
}

static void openTurnIntoMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "turn-into";
  overlay.title = "Turn into";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 260.0f;
  overlay.filterable = true;
  overlay.placeholder = "Filter block types";
  overlay.items = blockKindItems();
  ui.overlays.open(std::move(overlay));
}

static void openBlockMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "block-menu";
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  overlay.width = 240.0f;
  // Fold is offered only where the document already nests something to hide.
  const auto blocks = doc::scanBlocks(ui.editor.text());
  const std::size_t head = foldHeadFor(blocks, doc::blockIndexAt(blocks, ui.editor.cursor()));
  const bool folds = head < blocks.size();
  const bool folded = folds && ui.folds.folded(ui.state.selection().noteId, doc::foldKey(ui.editor.text(), blocks[head]));
  overlay.items = {
    {"turn", "Turn into", "", "Ctrl+Shift+1-9", true, false},
    {"duplicate", "Duplicate", "", "Ctrl+D", true, false},
    {"fold", folded ? "Unfold" : "Fold", "", "Ctrl+.", folds, false},
    {"move-up", "Move up", "", "Alt+Up", true, false},
    {"move-down", "Move down", "", "Alt+Down", true, false},
    {"delete", "Delete", "", "Ctrl+Shift+D", true, true},
  };
  ui.overlays.open(std::move(overlay));
}

// `slashStart` is the "/" the user typed; committing erases [slashStart, caret)
// before the block transform runs.
static void openSlashMenu(UiRuntime& ui, std::size_t slashStart) {
  ui.slashStart = slashStart;
  ui.slashInserts = false;
  clearBlockSelection(ui);
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "slash-menu";
  overlay.title = "Insert block";
  overlay.width = 320.0f;
  overlay.filterable = true;
  overlay.placeholder = "Filter block types";
  overlay.hint = "Enter inserts, Esc keeps typing";
  overlay.items = blockKindItems();
  ui.overlays.open(std::move(overlay));
}

// Opened from the gutter's insert button: nothing is written until a block type
// is chosen, so dismissing the menu leaves the note exactly as it was.
static void openInsertMenu(UiRuntime& ui, std::size_t blockStart) {
  openSlashMenu(ui, ui.editor.cursor());
  ui.slashInserts = true;
  ui.slashAfterBlock = blockStart;
}

static void commitSlashMenu(UiRuntime& ui, const std::string& itemId) {
  const BlockKindEntry* entry = blockKindFor(itemId);
  if(ui.slashInserts) {
    if(entry && applyEdit(ui, doc::insertBlockAfter(ui.editor.text(), ui.slashAfterBlock, entry->kind, entry->level))) {
      ui.status = entry->label;
    }
    return;
  }
  const std::size_t caret = ui.editor.cursor();
  const std::size_t start = std::min(ui.slashStart, caret);
  if(start < caret) {
    ui.editor.replaceRange(start, caret, "");
    markEdited(ui);
  }
  performBlockCommand(ui, itemId);
}

// Everything the shell can do, in one list. The palette exists so a feature is
// discoverable without a status-bar hint line naming it, so a command that is
// only reachable by shortcut does not belong here - it belongs in both.
struct PaletteCommand {
  const char* id;
  const char* label;
  const char* shortcut;
  // Commands that need something selected are listed but refused, rather than
  // hidden: a palette that changes shape is a palette you cannot learn.
  bool needsNote;
};

static constexpr PaletteCommand kCommands[] = {
  {"jump", "Go to note...", "Ctrl+P", false},
  {"new-note", "New note", "Ctrl+N", false},
  {"new-folder", "New notebook", "", false},
  {"save", "Save note", "Ctrl+S", true},
  {"rename", "Rename note...", "F2", true},
  {"icon", "Set note icon...", "", true},
  {"tags", "Edit tags...", "Ctrl+T", true},
  {"favorite", "Toggle favorite", "", true},
  {"move-note", "Move note to notebook...", "", true},
  {"move-blocks", "Move selected blocks to note...", "", true},
  {"delete-note", "Delete note...", "", true},
  {"rename-folder", "Rename notebook...", "", false},
  {"delete-folder", "Delete notebook...", "", false},
  {"restore", "Restore from trash...", "", false},
  {"fold", "Fold or unfold section", "Ctrl+.", true},
  {"theme", "Toggle light and dark", "Ctrl+Shift+L", false},
  {"settings", "Settings...", "Ctrl+,", false},
  {"shortcuts", "Keyboard shortcuts...", "F1", false},
  {"refresh", "Refresh library", "Ctrl+R", false},
  {"pane-live", "View: live", "Ctrl+1", false},
  {"pane-raw", "View: raw Markdown", "Ctrl+2", false},
  {"pane-reading", "View: reading", "Ctrl+3", false},
  {"pane-split", "View: split", "Ctrl+4", false},
};

static void performCommand(UiRuntime& ui, const std::string& id);
static void openDeleteFolderConfirm(UiRuntime& ui);

static void openCommandPalette(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "command-palette";
  overlay.title = "Commands";
  overlay.filterable = true;
  overlay.placeholder = "Type a command";
  overlay.hint = "Enter run   Esc cancel";
  overlay.width = 460.0f;
  for(const auto& command : kCommands) {
    overlay.items.push_back({command.id, command.label, "", command.shortcut, true, false});
  }
  ui.overlays.open(std::move(overlay));
}

// One list of every note in the library, reused by "go to note" and by anything
// that has to name a target note. `id` says which, so the result knows what it
// is answering.
static void openNotePalette(UiRuntime& ui, std::string overlayId, std::string title) {
  ui::Overlay overlay;
  overlay.id = std::move(overlayId);
  overlay.title = std::move(title);
  overlay.filterable = true;
  overlay.placeholder = "Type a note title";
  overlay.hint = "Enter open   Esc cancel";
  overlay.width = 460.0f;
  const auto root = ui.state.libraryRoot();
  for(const auto& note : ui.state.allNotes()) {
    const auto folder = note.path.lexically_relative(root).parent_path().generic_string();
    overlay.items.push_back({note.id,
                             note.icon.empty() ? note.title : note.icon + " " + note.title,
                             folder.empty() ? root.filename().generic_string() : folder,
                             "", true, false});
  }
  if(overlay.items.empty()) {
    ui.status = "No notes to jump to";
    return;
  }
  ui.overlays.open(std::move(overlay));
}

static void openFolderPalette(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "move-note-folder";
  overlay.title = "Move note to";
  overlay.filterable = true;
  overlay.placeholder = "Type a notebook name";
  overlay.hint = "Enter move   Esc cancel";
  overlay.width = 420.0f;
  const auto rootLabel = ui.state.libraryRoot().filename().generic_string();
  for(const auto& folder : ui.state.folders()) {
    overlay.items.push_back({folder.path.generic_string().empty() ? "/" : folder.path.generic_string(),
                             folder.path.empty() ? rootLabel : folder.path.generic_string(),
                             "", std::to_string(folder.noteCount), true, false});
  }
  ui.overlays.open(std::move(overlay));
}

static void openTrashPalette(UiRuntime& ui) {
  const auto entries = ui.state.trashEntries();
  if(entries.empty()) {
    ui.status = "Trash is empty";
    return;
  }
  ui::Overlay overlay;
  overlay.id = "restore-trash";
  overlay.title = "Restore from trash";
  overlay.filterable = true;
  overlay.placeholder = "Type a name";
  overlay.hint = "Enter restore   Esc cancel";
  overlay.width = 460.0f;
  for(const auto& entry : entries) {
    overlay.items.push_back({entry.name, entry.title,
                             entry.originalRelative.parent_path().generic_string(), entry.deletedAt, true, false});
  }
  ui.overlays.open(std::move(overlay));
}

static void openIconPrompt(UiRuntime& ui) {
  const auto note = ui.state.selectedNote();
  if(!note) {
    ui.status = "No note selected";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "note-icon";
  overlay.title = "Note icon";
  overlay.value = note->metadata.icon;
  overlay.valueSelected = !overlay.value.empty();
  overlay.placeholder = "One emoji";
  overlay.hint = "Enter save   Esc cancel   empty removes the icon";
  ui.overlays.open(std::move(overlay));
}

// `~` for the home directory, as everything else that prints a path does.
static std::string displayPath(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  const char* home = std::getenv("HOME");
  if(!home || !*home) return text;
  const std::string prefix(home);
  if(text.rfind(prefix, 0) != 0) return text;
  if(text.size() == prefix.size()) return "~";
  if(text[prefix.size()] != '/') return text;
  return "~" + text.substr(prefix.size());
}

// Settings are a list of what can change and what it is now; each row opens the
// list of its own values. A list overlay rather than a panel of widgets,
// because the keyboard, the filter and the dismissal rules are then the ones
// already learnt from every other overlay in the app.
static void openSettings(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "settings";
  overlay.title = "Settings";
  overlay.filterable = true;
  overlay.placeholder = "Type a setting";
  overlay.hint = "Enter change   Esc close";
  overlay.width = 480.0f;
  overlay.items.push_back({"theme", "Theme",
                           ui::themeMode() == ui::ThemeMode::Dark ? "Dark" : "Light", "Ctrl+Shift+L", true, false});
  overlay.items.push_back({"text-size", "Text size", std::string(ui::textSizeLabel(ui::textSize())), "", true, false});
  overlay.items.push_back({"page-width", "Page width", std::string(ui::pageWidthLabel(ui::pageWidth())), "", true, false});
  // Trimmed from the left: a truncated path keeps the half that says which
  // folder this is, not the half every path on the machine shares.
  std::string library = ui.state.hasLibrary() ? displayPath(ui.state.libraryRoot()) : std::string("none");
  if(library.size() > 34) {
    const auto root = ui.state.libraryRoot();
    library = "\xe2\x80\xa6/" + root.parent_path().filename().generic_string() + "/" + root.filename().generic_string();
  }
  overlay.items.push_back({"library", "Library folder", library, "", true, false});
  overlay.items.push_back({"shortcuts", "Keyboard shortcuts...", "", "F1", true, false});
  ui.overlays.open(std::move(overlay));
}

// The values one setting can take. "current" rather than a tick: the vendored
// UI face is not guaranteed a check glyph, and a word cannot render as tofu.
static void openSettingsValues(UiRuntime& ui, const std::string& which) {
  ui::Overlay overlay;
  overlay.width = 380.0f;
  overlay.hint = "Enter apply   Esc back";
  const auto mark = [](bool active) { return active ? "current" : ""; };
  if(which == "theme") {
    overlay.id = "settings-theme";
    overlay.title = "Theme";
    overlay.items.push_back({"light", "Light", "", mark(ui::themeMode() == ui::ThemeMode::Light), true, false});
    overlay.items.push_back({"dark", "Dark", "", mark(ui::themeMode() == ui::ThemeMode::Dark), true, false});
  } else if(which == "text-size") {
    overlay.id = "settings-text-size";
    overlay.title = "Text size";
    for(const auto size : {ui::TextSize::Small, ui::TextSize::Medium, ui::TextSize::Large}) {
      overlay.items.push_back({std::string(ui::textSizeName(size)), std::string(ui::textSizeLabel(size)),
                               "", mark(ui::textSize() == size), true, false});
    }
  } else if(which == "page-width") {
    overlay.id = "settings-page-width";
    overlay.title = "Page width";
    for(const auto width : {ui::PageWidth::Narrow, ui::PageWidth::Medium, ui::PageWidth::Wide}) {
      overlay.items.push_back({std::string(ui::pageWidthName(width)), std::string(ui::pageWidthLabel(width)),
                               "", mark(ui::pageWidth() == width), true, false});
    }
  } else {
    return;
  }
  ui.overlays.open(std::move(overlay));
}

static void openLibraryPrompt(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "settings-library";
  overlay.title = "Library folder";
  overlay.value = ui.state.hasLibrary() ? displayPath(ui.state.libraryRoot()) : std::string {};
  overlay.valueSelected = !overlay.value.empty();
  overlay.placeholder = "~/Notes";
  overlay.hint = "Enter open   Esc cancel   a folder that is not there is created";
  overlay.width = 520.0f;
  ui.overlays.open(std::move(overlay));
}

// Opens a different library without restarting. The one being left is written
// out first, so its open note, favorites and folds go with it rather than
// following the user into the new one.
static void switchLibrary(UiRuntime& ui, const std::string& typed) {
  std::string value = typed;
  while(!value.empty() && (value.back() == ' ' || value.back() == '/')) value.pop_back();
  if(value.empty()) {
    ui.status = "Give a folder to open";
    return;
  }
  // `~` is the shell's, and nothing expanded it on the way into a text field.
  if(value == "~" || value.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    if(!home || !*home) {
      ui.status = "No HOME to expand ~ against";
      return;
    }
    value = std::string(home) + value.substr(1);
  }
  const std::filesystem::path root(value);
  std::error_code ec;
  if(ui.state.hasLibrary() && std::filesystem::equivalent(root, ui.state.libraryRoot(), ec) && !ec) {
    ui.status = "Already open";
    return;
  }
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty()) (void)saveCurrent(ui, true);
  persistLibraryState(ui);
  try {
    if(!openLibraryRoot(ui, root)) {
      ui.status = "Could not open " + displayPath(root);
      return;
    }
  } catch(const std::exception& error) {
    ui.status = "Could not open " + displayPath(root) + ": " + error.what();
    return;
  }
  writeConfiguredLibraryRoot(root);
  ui.status = "Opened " + displayPath(root);
}

// The shortcut list, and the only place the bindings are written down for the
// user. A row with no keys is a section heading: it is listed as a disabled
// item, so the arrows step over it and Enter cannot land on it.
struct ShortcutRow {
  const char* keys;
  const char* what;
};

static constexpr ShortcutRow kShortcuts[] = {
  {"", "Getting around"},
  {"Ctrl+P", "Go to any note"},
  {"Ctrl+Shift+P", "Command palette"},
  {"Ctrl+F", "Find in this note"},
  {"Ctrl+Shift+F", "Search every note"},
  {"F1", "This list"},
  {"Ctrl+,", "Settings"},
  {"Up, Down", "Walk the sidebar"},
  {"Right, Left", "Open or close a notebook"},

  {"", "Notes and notebooks"},
  {"Ctrl+N", "New note"},
  {"Ctrl+S", "Save now"},
  {"Ctrl+R", "Reload the library from disk"},
  {"F2", "Rename the note"},
  {"Ctrl+T", "Edit tags"},

  {"", "Writing"},
  {"Ctrl+B, Ctrl+I, Ctrl+E", "Bold, italic, code"},
  {"Ctrl+K", "Link the selection"},
  {"Ctrl+Z, Ctrl+Y", "Undo, redo"},
  {"Ctrl+Left, Ctrl+Right", "Move by word"},
  {"Ctrl+Home, Ctrl+End", "Start and end of the note"},
  {"PageUp, PageDown", "Move by a screenful"},
  {"Tab, Shift+Tab", "Indent, outdent a list item"},
  {"Enter", "Continue the list, or leave it when empty"},
  {"Ctrl+Enter", "Tick or untick a task"},
  {"Ctrl+V, Ctrl+Shift+V", "Paste, paste an image as an attachment"},

  {"", "Blocks"},
  {"/", "Insert a block"},
  {"Esc", "Select the block, again to go back"},
  {"Shift+Up, Shift+Down", "Extend the block selection"},
  {"Alt+Up, Alt+Down", "Move the block"},
  {"Ctrl+D, Ctrl+Shift+D", "Duplicate, delete the block"},
  {"Ctrl+.", "Fold or unfold the section"},
  {"Ctrl+Shift+0..3", "Turn into text or a heading"},
  {"Ctrl+Shift+7, 8, 9", "Turn into a numbered item, bullet, task"},

  {"", "View"},
  {"Ctrl+1", "Live"},
  {"Ctrl+2", "Raw Markdown"},
  {"Ctrl+3", "Reading"},
  {"Ctrl+4", "Split"},
  {"Ctrl+L", "Cycle the four views"},
  {"Ctrl+Shift+L", "Light or dark"},
};

static void openShortcutHelp(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "shortcuts";
  overlay.title = "Keyboard shortcuts";
  overlay.filterable = true;
  overlay.placeholder = "Type to filter";
  overlay.hint = "Esc close";
  overlay.width = 520.0f;
  // Reference material, not a menu: as much of it on screen at once as the
  // window will hold.
  overlay.maxRows = 20;
  for(const auto& row : kShortcuts) {
    const bool heading = *row.keys == '\0';
    overlay.items.push_back({"", row.what, row.keys, "", !heading, false});
  }
  ui.overlays.open(std::move(overlay));
}

// Appends the selected blocks to another note and removes them from this one.
// The source edit goes through the undo stack as any block edit does; the
// target is not open, so it is written directly.
static void moveBlocksToNote(UiRuntime& ui, const std::string& targetId) {
  const auto target = ui.state.findNote(targetId);
  if(!target || target->id == ui.state.selection().noteId) {
    ui.status = "Pick a different note";
    return;
  }
  const auto [from, to] = blockSelectionCarets(ui);
  const auto& source = ui.editor.text();
  const auto blocks = doc::scanBlocks(source);
  const auto& first = blocks[doc::blockIndexAt(blocks, std::min(from, source.size()))];
  const auto& last = blocks[doc::blockIndexAt(blocks, std::min(to, source.size()))];
  std::string moved = source.substr(first.start, last.end - first.start);
  while(!moved.empty() && moved.back() == '\n') moved.pop_back();
  if(moved.empty()) {
    ui.status = "Nothing to move";
    return;
  }
  if(!ui.state.appendToNote(target->id, moved)) {
    ui.status = "Move failed";
    return;
  }
  if(applyEdit(ui, doc::deleteBlocks(source, from, to))) {
    clearBlockSelection(ui);
    ui.status = "Moved blocks to " + target->title;
  }
}

static void performCommand(UiRuntime& ui, const std::string& id) {
  if(id == "jump") openNotePalette(ui, "jump-note", "Go to note");
  else if(id == "new-note") createNote(ui);
  else if(id == "new-folder") beginFolderCreate(ui);
  else if(id == "save") saveCurrent(ui);
  else if(id == "rename") beginRename(ui);
  else if(id == "icon") openIconPrompt(ui);
  else if(id == "tags") beginTagEdit(ui);
  else if(id == "favorite") {
    const auto noteId = ui.state.selection().noteId;
    if(noteId.empty()) ui.status = "No note selected";
    else ui.status = ui.state.toggleFavorite(noteId) ? "Added to favorites" : "Removed from favorites";
  }
  else if(id == "move-note") openFolderPalette(ui);
  else if(id == "move-blocks") {
    if(!ui.blockSelectActive) ui.status = "Select blocks first with Esc";
    else openNotePalette(ui, "move-blocks-target", "Move blocks to");
  }
  else if(id == "delete-note") openDeleteNoteConfirm(ui);
  else if(id == "rename-folder") beginFolderRename(ui);
  else if(id == "delete-folder") openDeleteFolderConfirm(ui);
  else if(id == "restore") openTrashPalette(ui);
  else if(id == "fold") toggleFoldAt(ui, ui.editor.cursor());
  else if(id == "theme") {
    ui::setThemeMode(ui::themeMode() == ui::ThemeMode::Light ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
    ui.status = ui::themeMode() == ui::ThemeMode::Light ? "Light theme" : "Dark theme";
  }
  else if(id == "settings") openSettings(ui);
  else if(id == "shortcuts") openShortcutHelp(ui);
  else if(id == "refresh") {
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  }
  else if(id == "pane-live") setPaneMode(ui, ui::PaneMode::Live);
  else if(id == "pane-raw") setPaneMode(ui, ui::PaneMode::Editor);
  else if(id == "pane-reading") setPaneMode(ui, ui::PaneMode::Viewer);
  else if(id == "pane-split") setPaneMode(ui, ui::PaneMode::Split);
}

static void openDeleteNoteConfirm(UiRuntime& ui) {
  auto note = ui.state.selectedNote();
  if(!note) {
    ui.status = "Select a note before deleting";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::Confirm;
  overlay.id = "delete-note";
  overlay.title = "Delete \"" + note->item.title + "\"?";
  overlay.hint = "This cannot be undone.";
  overlay.confirmLabel = "Delete";
  overlay.width = 380.0f;
  ui.overlays.open(std::move(overlay));
}

static void openDeleteFolderConfirm(UiRuntime& ui) {
  if(ui.state.selection().folder.empty()) {
    ui.status = "Root notebook cannot be deleted";
    return;
  }
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::Confirm;
  overlay.id = "delete-folder";
  overlay.title = "Delete notebook \"" + ui.state.selection().folder.generic_string() + "\"?";
  overlay.hint = "Every note inside it is deleted too. This cannot be undone.";
  overlay.confirmLabel = "Delete";
  overlay.width = 420.0f;
  ui.overlays.open(std::move(overlay));
}

static void handleOverlayResult(UiRuntime& ui, const ui::OverlayResult& result) {
  if(result.overlayId == "rename-note") {
    ui.renameDraft = result.value;
    saveRename(ui);
  } else if(result.overlayId == "tags") {
    ui.tagDraft = result.value;
    saveTags(ui);
  } else if(result.overlayId == "folder-name") {
    ui.folderRenameDraft = result.value;
    saveFolderRename(ui);
  } else if(result.overlayId == "delete-note") {
    deleteSelected(ui);
  } else if(result.overlayId == "delete-folder") {
    deleteSelectedFolder(ui);
  } else if(result.overlayId == "note-menu") {
    if(result.itemId == "new") createNote(ui);
    else if(result.itemId == "rename") beginRename(ui);
    else if(result.itemId == "delete") openDeleteNoteConfirm(ui);
    // The rest are the palette's, so the menu and the palette cannot drift.
    else if(result.itemId == "move") performCommand(ui, "move-note");
    else performCommand(ui, result.itemId);
  } else if(result.overlayId == "block-menu") {
    if(result.itemId == "turn") openTurnIntoMenu(ui, ui.mouseX, ui.mouseY);
    else performBlockCommand(ui, result.itemId);
  } else if(result.overlayId == "turn-into") {
    performBlockCommand(ui, result.itemId);
  } else if(result.overlayId == "slash-menu") {
    commitSlashMenu(ui, result.itemId);
  } else if(result.overlayId == "command-palette") {
    performCommand(ui, result.itemId);
  } else if(result.overlayId == "jump-note") {
    selectNoteById(ui, result.itemId);
    if(const auto note = ui.state.findNote(result.itemId)) {
      const auto folder = note->path.lexically_relative(ui.state.libraryRoot()).parent_path();
      ui.searchDraft.clear();
      ui.state.selectFolder(folder);
      ui.state.selectNote(result.itemId);
      ui.tree.reveal(folder);
    }
    ui.focus = FocusArea::Editor;
  } else if(result.overlayId == "move-blocks-target") {
    moveBlocksToNote(ui, result.itemId);
  } else if(result.overlayId == "move-note-folder") {
    const std::filesystem::path folder = result.itemId == "/" ? std::filesystem::path {} : std::filesystem::path {result.itemId};
    ui.status = ui.state.moveSelectedNoteToFolder(folder) ? "Moved note" : "Move note failed";
    ui.tree.reveal(folder);
  } else if(result.overlayId == "restore-trash") {
    ui.status = ui.state.restoreFromTrash(result.itemId) ? "Restored from trash" : "Restore failed";
  } else if(result.overlayId == "note-icon") {
    ui.status = ui.state.setSelectedNoteIcon(result.value) ? (result.value.empty() ? "Removed icon" : "Set icon")
                                                           : "Could not set icon";
  } else if(result.overlayId == "settings") {
    if(result.itemId == "library") openLibraryPrompt(ui);
    else if(result.itemId == "shortcuts") openShortcutHelp(ui);
    else openSettingsValues(ui, result.itemId);
  } else if(result.overlayId == "settings-theme") {
    ui::setThemeMode(result.itemId == "light" ? ui::ThemeMode::Light : ui::ThemeMode::Dark);
    // The list comes back with the new value on it: changing two settings
    // should not need the dialog opened twice.
    openSettings(ui);
  } else if(result.overlayId == "settings-text-size") {
    ui::setTextSize(ui::textSizeFromName(result.itemId));
    openSettings(ui);
  } else if(result.overlayId == "settings-page-width") {
    ui::setPageWidth(ui::pageWidthFromName(result.itemId));
    openSettings(ui);
  } else if(result.overlayId == "settings-library") {
    switchLibrary(ui, result.value);
  } else if(result.overlayId == "folder-menu") {
    if(result.itemId == "new-folder") beginFolderCreate(ui);
    else if(result.itemId == "new-note") createNoteInFolder(ui, ui.state.selection().folder);
    else if(result.itemId == "rename") beginFolderRename(ui);
    else if(result.itemId == "delete") openDeleteFolderConfirm(ui);
  }
}

static void handleText(UiRuntime& ui, const char* input) {
  if(!input) return;
  if(ui.overlays.active()) {
    ui.overlays.handleText(input);
    return;
  }
  if(auto* draft = focusedInput(ui)) {
    if(ui.inputAllSelected) draft->clear();
    *draft += input;
    ui.inputAllSelected = false;
    syncFocusedInput(ui);
  } else if(ui.focus == FocusArea::Editor) {
    // Typing is text editing, so it takes the caret back from a block selection
    // rather than replacing whole blocks with a character.
    clearBlockSelection(ui);
    ui.editor.insert(input);
    // "[] " only becomes a real task marker once the space lands, so the check
    // is cheap and runs at most once per typed space.
    if(std::string_view(input).find(' ') != std::string_view::npos) {
      applyTransform(ui, doc::applyMarkdownShortcut);
    }
    markEdited(ui);
    ui.revealEditorCursor = true;
    // "/" opens the block inserter, but only where a block could start: mid-word
    // slashes belong to paths and URLs.
    if(ui.state.shell().paneMode == ui::PaneMode::Live && std::string_view(input) == "/") {
      const std::size_t slash = ui.editor.cursor() - 1;
      const char before = slash == 0 ? '\n' : ui.editor.text()[slash - 1];
      if(before == '\n' || before == ' ' || before == '\t') openSlashMenu(ui, slash);
    }
  }
}

static void handleKey(UiRuntime& ui, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod) {
  const SDL_Keymod currentMod = SDL_GetModState();
  const bool ctrl = ((mod | currentMod) & SDL_KMOD_CTRL) != 0;
  const bool shift = ((mod | currentMod) & SDL_KMOD_SHIFT) != 0;
  const bool alt = ((mod | currentMod) & SDL_KMOD_ALT) != 0;
  const auto shortcut = [&](SDL_Keycode keycode, SDL_Scancode code) {
    return ctrl && (key == keycode || scancode == code);
  };
  if(ui.overlays.active()) {
    bool handled = false;
    const auto result = ui.overlays.handleKey(key, ctrl, shift, handled);
    if(result) handleOverlayResult(ui, *result);
    if(handled) return;
  }
  if(inputDebugEnabled()) {
    std::cerr << "input keydown"
              << " key=" << SDL_GetKeyName(key)
              << " keycode=0x" << std::hex << static_cast<Uint32>(key) << std::dec
              << " scancode=" << SDL_GetScancodeName(scancode)
              << " mod=0x" << std::hex << static_cast<Uint32>(mod)
              << " current_mod=0x" << static_cast<Uint32>(currentMod) << std::dec
              << " ctrl=" << ctrl
              << " focus=" << focusName(ui.focus)
              << " editor_selection=" << ui.editor.hasSelection()
              << " input_all_selected=" << ui.inputAllSelected
              << "\n";
  }
  if(key == SDLK_F1) {
    openShortcutHelp(ui);
  } else if(shortcut(SDLK_COMMA, SDL_SCANCODE_COMMA)) {
    openSettings(ui);
  } else if(shortcut(SDLK_P, SDL_SCANCODE_P)) {
    // Ctrl+Shift+P is every command; Ctrl+P is the notes, which is the jump
    // people reach for a hundred times more often.
    if(shift) openCommandPalette(ui);
    else openNotePalette(ui, "jump-note", "Go to note");
  } else if(shortcut(SDLK_N, SDL_SCANCODE_N)) {
    createNote(ui);
  } else if(shortcut(SDLK_S, SDL_SCANCODE_S)) {
    saveCurrent(ui);
  } else if(shortcut(SDLK_R, SDL_SCANCODE_R)) {
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  } else if(shortcut(SDLK_T, SDL_SCANCODE_T)) {
    beginTagEdit(ui);
  } else if(shortcut(SDLK_A, SDL_SCANCODE_A)) {
    if(ui.focus == FocusArea::Editor) {
      ui.editor.selectAll();
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    }
    else if(auto* input = focusedInput(ui)) {
      ui.inputAllSelected = !input->empty();
      if(ui.inputAllSelected) SDL_SetPrimarySelectionText(input->c_str());
    }
  } else if(shortcut(SDLK_C, SDL_SCANCODE_C)) {
    if(ui.focus == FocusArea::Editor && ui.blockSelectActive) {
      const auto [from, to] = blockSelectionCarets(ui);
      const auto blocks = doc::scanBlocks(ui.editor.text());
      const std::size_t start = blocks[doc::blockIndexAt(blocks, from)].start;
      const std::size_t end = blocks[doc::blockIndexAt(blocks, to)].end;
      ui.status = setClipboardText(std::string_view(ui.editor.text()).substr(start, end - start))
                    ? "Copied block" : "Copy failed: " + std::string(SDL_GetError());
    } else if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      ui.status = setClipboardText(ui.editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    } else if(auto* input = focusedInput(ui); input && ui.inputAllSelected) {
      ui.status = setClipboardText(*input) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_X, SDL_SCANCODE_X)) {
    if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      const bool copied = setClipboardText(ui.editor.selectedText());
      ui.editor.eraseSelection();
      markEdited(ui);
      ui.revealEditorCursor = true;
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    } else if(auto* input = focusedInput(ui); input && ui.inputAllSelected) {
      const bool copied = setClipboardText(*input);
      input->clear();
      ui.inputAllSelected = false;
      syncFocusedInput(ui);
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_Z, SDL_SCANCODE_Z)) {
    if(ui.focus == FocusArea::Editor && ui.editor.undo()) {
      markEdited(ui);
      ui.revealEditorCursor = true;
      ui.status = "Undo";
    }
  } else if(shortcut(SDLK_Y, SDL_SCANCODE_Y)) {
    if(ui.focus == FocusArea::Editor && ui.editor.redo()) {
      markEdited(ui);
      ui.revealEditorCursor = true;
      ui.status = "Redo";
    }
  } else if(shortcut(SDLK_V, SDL_SCANCODE_V)) {
    if(focusedInput(ui)) pasteClipboardIntoInput(ui);
    else if(ui.focus == FocusArea::Editor) {
      // Decide by what is actually on the clipboard: image data wins (image
      // copies often also expose an incidental text/plain target), otherwise
      // paste text. Shift forces plain text even when an image is present.
      if(shift) {
        if(!pasteClipboardText(ui)) pasteClipboardImage(ui);
      } else {
        if(!pasteClipboardImage(ui)) pasteClipboardText(ui);
      }
      ui.revealEditorCursor = true;
    }
  } else if(shortcut(SDLK_B, SDL_SCANCODE_B)) {
    wrapEditorSelection(ui, "**", "**", "Bold");
  } else if(shortcut(SDLK_I, SDL_SCANCODE_I)) {
    wrapEditorSelection(ui, "*", "*", "Italic");
  } else if(shortcut(SDLK_E, SDL_SCANCODE_E)) {
    wrapEditorSelection(ui, "`", "`", "Code");
  } else if(shortcut(SDLK_K, SDL_SCANCODE_K)) {
    // In the editor Ctrl+K makes a link out of the selection, as it does
    // everywhere else; outside it there is no selection to link, so it is the
    // jump the plan asked for.
    if(ui.focus == FocusArea::Editor) linkEditorSelection(ui);
    else openNotePalette(ui, "jump-note", "Go to note");
  } else if(shortcut(SDLK_PERIOD, SDL_SCANCODE_PERIOD)) {
    if(ui.focus == FocusArea::Editor) toggleFoldAt(ui, ui.editor.cursor());
  } else if(shortcut(SDLK_D, SDL_SCANCODE_D) && shift) {
    if(ui.focus == FocusArea::Editor) performBlockCommand(ui, "delete");
  } else if(shortcut(SDLK_D, SDL_SCANCODE_D)) {
    if(ui.focus == FocusArea::Editor) performBlockCommand(ui, "duplicate");
  } else if(shift && shortcut(SDLK_0, SDL_SCANCODE_0)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Paragraph, 0, "text");
  } else if(shift && shortcut(SDLK_1, SDL_SCANCODE_1)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Heading, 1, "heading 1");
  } else if(shift && shortcut(SDLK_2, SDL_SCANCODE_2)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Heading, 2, "heading 2");
  } else if(shift && shortcut(SDLK_3, SDL_SCANCODE_3)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Heading, 3, "heading 3");
  } else if(shift && shortcut(SDLK_7, SDL_SCANCODE_7)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Ordered, 0, "a numbered item");
  } else if(shift && shortcut(SDLK_8, SDL_SCANCODE_8)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Bullet, 0, "a bullet");
  } else if(shift && shortcut(SDLK_9, SDL_SCANCODE_9)) {
    turnCurrentBlockInto(ui, doc::BlockKind::Todo, 0, "a task");
  } else if(shortcut(SDLK_1, SDL_SCANCODE_1)) {
    setPaneMode(ui, ui::PaneMode::Live);
  } else if(shortcut(SDLK_2, SDL_SCANCODE_2)) {
    setPaneMode(ui, ui::PaneMode::Editor);
  } else if(shortcut(SDLK_3, SDL_SCANCODE_3)) {
    setPaneMode(ui, ui::PaneMode::Viewer);
  } else if(shortcut(SDLK_4, SDL_SCANCODE_4)) {
    setPaneMode(ui, ui::PaneMode::Split);
  } else if(shortcut(SDLK_L, SDL_SCANCODE_L) && shift) {
    const bool toDark = ui::themeMode() == ui::ThemeMode::Light;
    ui::setThemeMode(toDark ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
    ui.status = toDark ? "Dark theme" : "Light theme";
  } else if(shortcut(SDLK_L, SDL_SCANCODE_L)) {
    cyclePaneMode(ui);
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F) && shift) {
    if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
    ui.inputAllSelected = false;
    ui.focus = FocusArea::Search;
    ui.state.setSearch(ui.searchDraft, ui.searchScope);
    ui.status = "Search all notes";
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F)) {
    ui.inputAllSelected = false;
    ui.focus = FocusArea::Find;
    updateFindStatus(ui);
  } else if(key == SDLK_ESCAPE) {
    if(ui.focus == FocusArea::Search && !ui.searchDraft.empty()) {
      ui.searchDraft.clear();
      ui.state.setSearch("", ui.searchScope);
    }
    if(ui.focus == FocusArea::Find) ui.findDraft.clear();
    ui.creatingFolder = false;
    ui.inputAllSelected = false;
    // In the live surface Esc steps out of the text and selects the block
    // itself; a second Esc puts the caret back.
    if(ui.focus == FocusArea::Editor && ui.state.shell().paneMode == ui::PaneMode::Live) {
      if(ui.blockSelectActive) clearBlockSelection(ui);
      else selectBlockAtCursor(ui);
    }
    ui.focus = FocusArea::Editor;
  } else if(ui.focus == FocusArea::Search) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.searchDraft.clear();
      ui.inputAllSelected = false;
      ui.state.setSearch(ui.searchDraft, ui.searchScope);
      selectNoteAt(ui, 0);
    } else if(key == SDLK_BACKSPACE && !ui.searchDraft.empty()) {
      ui.searchDraft.pop_back();
      ui.state.setSearch(ui.searchDraft, ui.searchScope);
      selectNoteAt(ui, 0);
    } else if(key == SDLK_RETURN) {
      ui.focus = FocusArea::Editor;
    }
  } else if(ui.focus == FocusArea::Find) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.findDraft.clear();
      ui.inputAllSelected = false;
      updateFindStatus(ui);
    } else if(key == SDLK_BACKSPACE && !ui.findDraft.empty()) {
      ui.findDraft.pop_back();
      updateFindStatus(ui);
    } else if(key == SDLK_RETURN) {
      ui.focus = FocusArea::Editor;
    }
  } else if(ui.focus == FocusArea::Editor && ui.blockSelectActive) {
    // Selected blocks are objects: the arrows walk them, and one command acts
    // over the whole range.
    if(alt && (key == SDLK_UP || key == SDLK_DOWN)) {
      moveSelectedBlocks(ui, key == SDLK_UP ? -1 : 1);
    } else if(key == SDLK_UP || key == SDLK_DOWN) {
      moveBlockSelection(ui, key == SDLK_UP ? -1 : 1, shift);
    } else if(key == SDLK_BACKSPACE || key == SDLK_DELETE) {
      performBlockCommand(ui, "delete");
    } else if(key == SDLK_TAB) {
      applyTransform(ui, shift ? doc::outdent : doc::indent);
      syncBlockSelectionToEdit(ui);
    } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      // Enter puts the caret back into the first selected block's text.
      const auto blocks = doc::scanBlocks(ui.editor.text());
      const auto content = blocks[doc::blockIndexAt(blocks, blockSelectionCarets(ui).first)].contentStart;
      clearBlockSelection(ui);
      ui.editor.moveCursor(content);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_LEFT || key == SDLK_RIGHT) {
      clearBlockSelection(ui);
    }
  } else if(ui.focus == FocusArea::Editor) {
    const bool live = ui.state.shell().paneMode == ui::PaneMode::Live;
    // One visual row up or down: the live surface wraps, the raw editor does not.
    const auto rowStep = [&](int rows) {
      return live ? ui.livePage.rowRelative(ui.editor.cursor(), rows) : ui.editor.cursor();
    };
    if(key == SDLK_BACKSPACE) {
      if(ctrl) ui.editor.erasePreviousWord();
      // Against a block's first character, Backspace strips the block's marker
      // before it starts eating the block above.
      else if(ui.editor.hasSelection() || !applyTransform(ui, doc::outdentOrUnwrap)) ui.editor.erasePrevious();
      markEdited(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DELETE) {
      if(ctrl) ui.editor.eraseNextWord();
      else ui.editor.eraseNext();
      markEdited(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      if(ctrl) {
        if(!applyTransform(ui, doc::toggleTodo)) ui.status = "No task to toggle here";
      } else if(ui.editor.hasSelection() ||
                (!applyTransform(ui, doc::closeFence) && !applyTransform(ui, doc::continueList))) {
        ui.editor.insert("\n");
        markEdited(ui);
        ui.revealEditorCursor = true;
      }
    } else if(key == SDLK_TAB) {
      if(shift) {
        applyTransform(ui, doc::outdent);
      } else if(!applyTransform(ui, doc::indent)) {
        ui.editor.insert("  ");
        markEdited(ui);
        ui.revealEditorCursor = true;
      }
    } else if(key == SDLK_LEFT) {
      if(ctrl) ui.editor.moveWordLeft(shift);
      else ui.editor.moveLeft(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RIGHT) {
      if(ctrl) ui.editor.moveWordRight(shift);
      else ui.editor.moveRight(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_UP) {
      if(alt) moveSelectedBlocks(ui, -1);
      else if(live) ui.editor.moveTo(rowStep(-1), shift);
      else ui.editor.moveLineUp(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DOWN) {
      if(alt) moveSelectedBlocks(ui, 1);
      else if(live) ui.editor.moveTo(rowStep(1), shift);
      else ui.editor.moveLineDown(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
      const int direction = key == SDLK_PAGEUP ? -1 : 1;
      if(live) {
        const int rows = static_cast<int>(std::max<std::size_t>(1, ui.livePage.rowsPerPage()));
        ui.editor.moveTo(rowStep(direction * rows), shift);
      } else {
        for(int i = 0; i < std::max(1, ui.editorVisibleRows); ++i) {
          if(direction < 0) ui.editor.moveLineUp(shift);
          else ui.editor.moveLineDown(shift);
        }
      }
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_HOME) {
      if(ctrl) ui.editor.moveDocumentStart(shift);
      else ui.editor.moveLineStart(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_END) {
      if(ctrl) ui.editor.moveDocumentEnd(shift);
      else ui.editor.moveLineEnd(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    }
  } else if(ui.focus == FocusArea::Notes) {
    if(key == SDLK_DOWN) selectNoteAt(ui, ui.noteCursor + 1);
    else if(key == SDLK_UP) selectNoteAt(ui, ui.noteCursor - 1);
    else if(key == SDLK_RETURN) ui.focus = FocusArea::Editor;
  } else if(ui.focus == FocusArea::Folders) {
    if(key == SDLK_DOWN || key == SDLK_UP) moveTreeCursor(ui, key == SDLK_DOWN ? 1 : -1);
    else if(key == SDLK_RIGHT || key == SDLK_LEFT) expandTreeCursor(ui, key == SDLK_RIGHT);
    else if(key == SDLK_RETURN) ui.focus = FocusArea::Editor;
  }
}

static void handleMouse(TextRenderer& text, UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(ui.overlays.active()) {
    bool handled = false;
    const auto result = ui.overlays.handleClick(x, y, handled);
    if(result) handleOverlayResult(ui, *result);
    if(handled) return;
  }
  const AppLayout layout = computeLayout(ui.state.shell(), width, height);

  if(button == SDL_BUTTON_MIDDLE) {
    if(contains(layout.notes, x, y) && y >= layout.notes.y + 12 && y <= layout.notes.y + 46) {
      ui.focus = FocusArea::Search;
      ui.inputAllSelected = false;
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y) && ui.state.shell().paneMode == ui::PaneMode::Live) {
      ui.focus = FocusArea::Editor;
      ui.editor.moveCursor(ui.livePage.offsetAt(x, y));
      ui.revealEditorCursor = true;
      ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y)) {
      Rect editorRect = layout.content;
      bool editorAtPoint = ui.state.shell().paneMode == ui::PaneMode::Editor;
      if(ui.state.shell().paneMode == ui::PaneMode::Split) {
        editorRect.w = layout.content.w / 2.0f;
        editorAtPoint = contains(editorRect, x, y);
      }
      if(editorAtPoint) {
        ui.focus = FocusArea::Editor;
        placeEditorCursor(text, ui, editorRect, x, y);
        ui.revealEditorCursor = true;
        ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
        return;
      }
    }
    if(focusedInput(ui)) {
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
    }
    return;
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && ui.state.shell().paneMode == ui::PaneMode::Live) {
    const int maxScroll = ui.livePage.maxScroll();
    const auto thumb = scrollbarThumb(ui.livePage.pageRect(), ui.livePage.scroll(), maxScroll);
    if(maxScroll > 0 && contains(scrollbarHitRect(thumb), x, y)) {
      ui.scrollDragTarget = ScrollDragTarget::Live;
      ui.scrollDragOffsetY = y - thumb.y;
      ui.focus = FocusArea::Editor;
      return;
    }
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && ui.state.shell().paneMode != ui::PaneMode::Live) {
    Rect editorRect = layout.content;
    Rect viewerRect = layout.content;
    bool hasEditor = false;
    bool hasViewer = false;
    if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
      hasEditor = true;
    } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
      hasViewer = true;
    } else {
      hasEditor = true;
      hasViewer = true;
      editorRect.w = layout.content.w / 2.0f;
      viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
    }
    if(hasEditor) {
      Rect writing {editorRect.x + 8, editorRect.y + 8, editorRect.w - 16, editorRect.h - 28};
      const int maxScroll = editorMaxScroll(text, ui, editorRect);
      const auto thumb = scrollbarThumb(writing, ui.editorScroll, maxScroll);
      if(maxScroll > 0 && contains(scrollbarHitRect(thumb), x, y)) {
        ui.scrollDragTarget = ScrollDragTarget::Editor;
        ui.scrollDragOffsetY = y - thumb.y;
        ui.focus = FocusArea::Editor;
        ui.revealEditorCursor = false;
        return;
      }
    }
    if(hasViewer) {
      Rect page {viewerRect.x + 8, viewerRect.y + 8, viewerRect.w - 16, viewerRect.h - 28};
      const int maxScroll = viewerMaxScroll(text, ui, viewerRect);
      const auto thumb = scrollbarThumb(page, ui.viewerScroll, maxScroll);
      if(maxScroll > 0 && contains(scrollbarHitRect(thumb), x, y)) {
        ui.scrollDragTarget = ScrollDragTarget::Viewer;
        ui.scrollDragOffsetY = y - thumb.y;
        ui.focus = FocusArea::Viewer;
        return;
      }
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    if(std::abs(x - (layout.sidebar.x + layout.sidebar.w)) <= 4.0f) {
      ui.resizingSidebar = true;
      return;
    }
    if(std::abs(x - (layout.notes.x + layout.notes.w)) <= 4.0f) {
      ui.resizingNotes = true;
      return;
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    for(const auto& region : ui.buttonRegions) {
      if(contains(region.rect, x, y)) {
        performAction(ui, region.action);
        return;
      }
    }
  }


  if(contains(layout.sidebar, x, y)) {
    ui.focus = FocusArea::Folders;
    const auto index = sidebarRowAt(ui, layout.sidebar, x, y);
    if(!index) {
      if(button == SDL_BUTTON_RIGHT) openFolderMenu(ui, x, y);
      return;
    }
    const SidebarRow row = ui.sidebarRows[*index];
    ui.folderCursor = static_cast<int>(*index);
    // The disclosure triangle opens a notebook without making it the selection:
    // looking inside one is not the same as switching to it.
    if(row.kind == SidebarRow::Kind::Tree && row.disclosure.w > 0.0f && contains(row.disclosure, x, y) &&
       button == SDL_BUTTON_LEFT) {
      ui.tree.toggle(row.tree.folder);
      return;
    }
    activateSidebarRow(ui, row, true);
    if(button == SDL_BUTTON_RIGHT) {
      if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == ui::TreeRowKind::Note) openNoteMenu(ui, x, y);
      else if(row.kind == SidebarRow::Kind::Tree) openFolderMenu(ui, x, y);
      return;
    }
    if(button == SDL_BUTTON_LEFT && row.kind == SidebarRow::Kind::Tree) {
      if(row.tree.kind == ui::TreeRowKind::Note) {
        ui.draggingNote = true;
        ui.draggingNoteId = row.tree.noteId;
      } else if(!row.tree.folder.empty()) {
        ui.draggingFolder = true;
        ui.draggingFolderPath = row.tree.folder;
      }
    }
    return;
  }
  if(contains(layout.notes, x, y)) {
    if(y >= layout.notes.y + 12 && y <= layout.notes.y + 46) {
      if(contains(ui.searchScopeToggle, x, y)) {
        ui.searchScope = nextSearchScope(ui.searchScope);
        ui.state.setSearch(ui.searchDraft, ui.searchScope);
        ui.status = "Search scope " + searchScopeLabel(ui.searchScope);
        return;
      }
      ui.inputAllSelected = false;
      ui.focus = FocusArea::Search;
      return;
    }
    ui.focus = FocusArea::Notes;
    if(!ui.searchDraft.empty()) {
      float rowY = layout.notes.y + 62.0f;
      for(const auto& result : ui.state.currentSearchResults()) {
        const std::size_t snippetCount = std::max<std::size_t>(result.snippets.size(), result.matchLine.empty() ? 0 : 1);
        const float availableH = layout.notes.y + layout.notes.h - 24.0f - (rowY - 8.0f);
        const std::size_t maxVisibleSnippets = availableH <= 90.0f ? 0 : static_cast<std::size_t>((availableH - 30.0f) / 60.0f);
        const std::size_t visibleSnippets = std::min<std::size_t>(snippetCount, std::min<std::size_t>(4, maxVisibleSnippets));
        const float rowH = visibleSnippets > 0 ? 30.0f + static_cast<float>(visibleSnippets * 60) : 50.0f;
        Rect row {layout.notes.x + 10, rowY - 8, layout.notes.w - 20, rowH};
        if(contains(row, x, y)) {
          selectNoteById(ui, result.id);
          break;
        }
        rowY += rowH;
      }
    } else {
      int index = static_cast<int>((y - (layout.notes.y + 54.0f)) / 50.0f);
      selectNoteAt(ui, index);
      if(button == SDL_BUTTON_LEFT && !ui.state.selection().noteId.empty()) {
        ui.draggingNote = true;
        ui.draggingNoteId = ui.state.selection().noteId;
      }
    }
    if(button == SDL_BUTTON_RIGHT) {
      openNoteMenu(ui, x, y);
    }
    return;
  }
  if(contains(layout.crumbs, x, y)) {
    if(contains(ui.favoriteButton, x, y)) {
      const auto noteId = ui.state.selection().noteId;
      ui.status = ui.state.toggleFavorite(noteId) ? "Added to favorites" : "Removed from favorites";
      return;
    }
    for(const auto& [rect, folder] : ui.crumbs) {
      if(!contains(rect, x, y)) continue;
      if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
      ui.state.selectFolder(folder);
      ui.tree.reveal(folder);
      ui.searchDraft.clear();
      selectNoteAt(ui, 0);
      return;
    }
    return;
  }
  if(contains(layout.content, x, y)) {
    // The live surface's own chrome sits above the text, so a link underneath it
    // must not swallow the click.
    const bool overLiveChrome = ui.state.shell().paneMode == ui::PaneMode::Live &&
                                (ui.livePage.gutterAt(x, y).has_value() || !ui.livePage.toolbarAt(x, y).empty() ||
                                 ui.livePage.foldAt(x, y).has_value() || ui.livePage.copyButtonAt(x, y).has_value());
    if(ui.state.shell().paneMode != ui::PaneMode::Editor && !overLiveChrome) {
      for(const auto& link : ui.linkRegions) {
        if(contains(link.rect, x, y)) {
          const auto target = link.target;
          const auto hash = target.find('#');
          const auto filePart = hash == std::string::npos ? target : target.substr(0, hash);
          const auto anchorPart = hash == std::string::npos ? std::string() : target.substr(hash + 1);
          if(filePart.empty() && !anchorPart.empty()) {
            const auto anchor = anchorFor(anchorPart);
            auto found = ui.viewerAnchors.find(anchor);
            if(found == ui.viewerAnchors.end()) found = ui.viewerAnchors.find(anchorPart);
            if(found != ui.viewerAnchors.end()) {
              ui.viewerScroll = std::max(0, found->second);
              ui.focus = FocusArea::Viewer;
              ui.status = "Jumped to " + anchorPart;
            } else {
              ui.status = "Anchor not found: " + anchorPart;
            }
            return;
          }
          if(isRemoteTarget(target)) {
            ui.status = spawnDetached({"xdg-open", target}) ? "Opened " + target : "Open failed";
            return;
          }
          if(!anchorPart.empty()) {
            auto note = ui.state.selectedNote();
            const auto sameNote = filePart.empty() || (note && (note->item.path.filename() == std::filesystem::path(filePart).filename()));
            if(sameNote) {
              const auto anchor = anchorFor(anchorPart);
              auto found = ui.viewerAnchors.find(anchor);
              if(found != ui.viewerAnchors.end()) {
                ui.viewerScroll = std::max(0, found->second);
                ui.focus = FocusArea::Viewer;
                ui.status = "Jumped to " + anchorPart;
                return;
              }
            }
          }
          if(ui.state.hasLibrary()) {
            attachments::AttachmentService service;
            try {
              const auto command = service.openCommand(ui.state.libraryRoot(), filePart.empty() ? target : filePart);
              ui.status = spawnDetached(command) ? "Opened " + std::filesystem::path(filePart.empty() ? target : filePart).filename().string() : "Open failed";
            } catch(const std::exception&) {
              ui.status = "Unsafe or unavailable link path";
            }
            return;
          }
          ui.status = "No library for local link";
          return;
        }
      }
    }
    if(ui.state.shell().paneMode == ui::PaneMode::Live) {
      ui.focus = FocusArea::Editor;
      if(button == SDL_BUTTON_RIGHT) {
        if(const auto index = ui.livePage.blockAt(x, y)) {
          const auto& blocks = ui.livePage.document().blocks();
          if(*index < blocks.size() && !ui.blockSelectActive) {
            ui.editor.moveCursor(blocks[*index].start);
            selectBlockAtCursor(ui);
          }
        }
        openBlockMenu(ui, x, y);
        return;
      }
      if(button != SDL_BUTTON_LEFT) return;

      // The formatting toolbar floats over the page, so it has to win over the
      // text underneath it.
      if(const auto action = ui.livePage.toolbarAt(x, y); !action.empty()) {
        if(action == "bold") wrapEditorSelection(ui, "**", "**", "Bold");
        else if(action == "italic") wrapEditorSelection(ui, "*", "*", "Italic");
        else if(action == "code") wrapEditorSelection(ui, "`", "`", "Code");
        else if(action == "strike") wrapEditorSelection(ui, "~~", "~~", "Strikethrough");
        else if(action == "link") linkEditorSelection(ui);
        else if(action == "turn") openTurnIntoMenu(ui, x, y);
        return;
      }
      // The disclosure control and the code block's copy button are chrome:
      // they act, and leave the caret and the selection where they were.
      if(const auto fold = ui.livePage.foldAt(x, y)) {
        toggleFoldAt(ui, fold->blockStart);
        return;
      }
      if(const auto blockStart = ui.livePage.copyButtonAt(x, y)) {
        const auto& blocks = ui.livePage.document().blocks();
        const auto& block = blocks[doc::blockIndexAt(blocks, *blockStart)];
        const std::string_view source = ui.editor.text();
        const std::string body {source.substr(block.contentStart, block.contentEnd - block.contentStart)};
        ui.status = setClipboardText(body) ? "Copied code" : "Clipboard unavailable";
        return;
      }
      if(const auto hit = ui.livePage.gutterAt(x, y)) {
        const auto& blocks = ui.livePage.document().blocks();
        if(hit->insert) {
          openInsertMenu(ui, blocks[hit->blockIndex].start);
        } else {
          // Grabbing a block outside the selection selects just that one; inside
          // it, the whole selection comes along.
          const auto [from, to] = blockSelectionCarets(ui);
          const bool inside = ui.blockSelectActive && hit->blockStart >= from && hit->blockStart <= to;
          if(!inside) {
            ui.editor.moveCursor(hit->blockStart);
            selectBlockAtCursor(ui);
          }
          const auto [dragFrom, dragTo] = blockSelectionCarets(ui);
          ui.draggingBlock = true;
          ui.dragBlockAnchor = dragFrom;
          ui.dragBlockFocus = dragTo;
          ui.blockDropOffset.reset();
        }
        return;
      }
      if((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        if(ui.blockSelectActive) {
          const auto& blocks = ui.livePage.document().blocks();
          if(const auto index = ui.livePage.blockAt(x, y); index && *index < blocks.size()) {
            ui.blockSelectFocus = blocks[*index].start;
            ui.editor.moveCursor(blocks[*index].start);
          }
        } else {
          ui.editor.moveTo(ui.livePage.offsetAt(x, y), true);
          publishEditorPrimarySelection(ui);
        }
        ui.revealEditorCursor = true;
        return;
      }
      clearBlockSelection(ui);
      // A task checkbox is a control, not text: ticking it must not move the
      // caret or start a selection.
      if(const auto blockStart = ui.livePage.checkboxAt(x, y)) {
        const std::size_t caret = ui.editor.cursor();
        if(applyEdit(ui, doc::toggleTodo(ui.editor.text(), *blockStart))) {
          // The flip is a one-byte swap, so every other offset survives it.
          ui.editor.moveCursor(std::min(caret, ui.editor.text().size()));
          ui.revealEditorCursor = false;
          ui.status = "Toggled task";
        }
        return;
      }
      // Clicking into a block the scanner does not model drops it to raw text
      // so it stays editable.
      if(const auto index = ui.livePage.blockAt(x, y)) {
        const auto& blocks = ui.livePage.document().blocks();
        if(*index < blocks.size() && blocks[*index].kind == doc::BlockKind::Complex && !ui.livePage.rawOffset()) {
          ui.livePage.setRawOffset(blocks[*index].start);
          ui.editor.moveCursor(blocks[*index].start);
          ui.editor.clearSelection();
          ui.revealEditorCursor = true;
          ui.status = "Editing block as raw Markdown";
          return;
        }
      }
      ui.editor.moveCursor(ui.livePage.offsetAt(x, y));
      ui.revealEditorCursor = true;
      ui.selectingEditorText = true;
      ui.editorSelectionAnchor = ui.editor.cursor();
      const Uint64 now = SDL_GetTicks();
      ui.editorClickCount = now - ui.lastEditorClick < 450 ? ui.editorClickCount + 1 : 1;
      ui.lastEditorClick = now;
      if(ui.editorClickCount == 2) {
        selectWordAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
      } else if(ui.editorClickCount >= 3) {
        selectLineAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
        ui.editorClickCount = 0;
      }
      return;
    }
    if(ui.state.shell().paneMode == ui::PaneMode::Viewer) ui.focus = FocusArea::Viewer;
    else if(ui.state.shell().paneMode == ui::PaneMode::Split && x >= layout.content.x + layout.content.w / 2.0f) ui.focus = FocusArea::Viewer;
    else {
      ui.focus = FocusArea::Editor;
      Rect editorRect = layout.content;
      if(ui.state.shell().paneMode == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
      placeEditorCursor(text, ui, editorRect, x, y);
      ui.selectingEditorText = true;
      ui.editorSelectionAnchor = ui.editor.cursor();
      const Uint64 now = SDL_GetTicks();
      ui.editorClickCount = now - ui.lastEditorClick < 450 ? ui.editorClickCount + 1 : 1;
      ui.lastEditorClick = now;
      if(ui.editorClickCount == 2) {
        selectWordAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
      }
      else if(ui.editorClickCount >= 3) {
        selectLineAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
        ui.editorClickCount = 0;
      }
    }
  }
}

static void handleMouseUp(UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(button == SDL_BUTTON_LEFT) {
    if(ui.draggingBlock) {
      if(ui.blockDropOffset &&
         applyEdit(ui, doc::moveBlocksTo(ui.editor.text(), ui.dragBlockAnchor, ui.dragBlockFocus, *ui.blockDropOffset))) {
        syncBlockSelectionToEdit(ui);
        ui.status = "Moved block";
      }
      ui.draggingBlock = false;
      ui.blockDropOffset.reset();
    }
    if(ui.selectingEditorText) publishEditorPrimarySelection(ui);
    ui.resizingSidebar = false;
    ui.resizingNotes = false;
    ui.selectingEditorText = false;
    ui.scrollDragTarget = ScrollDragTarget::None;
  }
  if(button != SDL_BUTTON_LEFT || (!ui.draggingNote && !ui.draggingFolder)) return;
  const AppLayout layout = computeLayout(ui.state.shell(), width, height);
  const auto index = sidebarRowAt(ui, layout.sidebar, x, y);
  if(index && ui.sidebarRows[*index].kind == SidebarRow::Kind::Tree) {
    // A note row stands for the folder holding it, so dropping between two
    // notes does the obvious thing rather than nothing.
    const auto target = ui.sidebarRows[*index].tree.folder;
    if(ui.draggingNote) {
      selectNoteById(ui, ui.draggingNoteId);
      if(ui.state.moveSelectedNoteToFolder(target)) {
        ui.tree.reveal(target);
        ui.status = "Moved note to " + (target.empty() ? ui.state.libraryRoot().filename().generic_string() : target.generic_string());
      } else {
        ui.status = "Move note failed";
      }
    } else if(ui.state.moveFolderInto(ui.draggingFolderPath, target)) {
      ui.tree.reveal(target / ui.draggingFolderPath.filename());
      ui.status = "Moved notebook into " + (target.empty() ? ui.state.libraryRoot().filename().generic_string() : target.generic_string());
    } else if(target != ui.draggingFolderPath.parent_path() && target != ui.draggingFolderPath) {
      ui.status = "Cannot move a notebook into itself";
    }
  }
  ui.draggingNote = false;
  ui.draggingNoteId.clear();
  ui.draggingFolder = false;
  ui.draggingFolderPath.clear();
  ui.sidebarDropRow.reset();
}

}

ApplicationOptions parseArgs(int argc, char** argv) {
  ApplicationOptions options;
  for(int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if(arg == "--headless") {
      options.headless = true;
    } else if(arg == "--library" && i + 1 < argc) {
      options.libraryRoot = argv[++i];
    } else if(arg == "--set-library" && i + 1 < argc) {
      options.configuredLibraryRoot = std::filesystem::path(argv[++i]);
    } else if(arg == "--attach" && i + 1 < argc) {
      options.attachPath = argv[++i];
    } else if(arg == "--screenshot" && i + 1 < argc) {
      options.screenshotPath = argv[++i];
    } else if(arg == "--size" && i + 1 < argc) {
      const std::string value = argv[++i];
      const auto x = value.find('x');
      if(x != std::string::npos) {
        options.windowWidth = std::max(320, std::atoi(value.substr(0, x).c_str()));
        options.windowHeight = std::max(240, std::atoi(value.substr(x + 1).c_str()));
      }
    } else if(arg == "--theme" && i + 1 < argc) {
      options.theme = ui::themeModeFromName(argv[++i]);
    } else if(arg == "--scale" && i + 1 < argc) {
      options.scale = static_cast<float>(std::atof(argv[++i]));
    } else if(arg == "--pane" && i + 1 < argc) {
      const std::string value = argv[++i];
      if(value == "editor" || value == "raw") options.paneMode = 0;
      else if(value == "viewer" || value == "reading") options.paneMode = 1;
      else if(value == "split") options.paneMode = 2;
      else if(value == "live") options.paneMode = 3;
    } else if(arg == "--select" && i + 1 < argc) {
      options.selectTitle = argv[++i];
    } else if(arg == "--open" && i + 1 < argc) {
      options.openOverlay = argv[++i];
    }
  }
  return options;
}

int run(ApplicationOptions options) {
  microcore::perf::ScopeTimer startup("startup");
  UiRuntime ui;
  if(options.configuredLibraryRoot) {
    if(!writeConfiguredLibraryRoot(*options.configuredLibraryRoot)) {
      std::cerr << "Failed to write library path config: " << *options.configuredLibraryRoot << "\n";
      return 1;
    }
  }
  if(options.libraryRoot.empty()) {
    if(auto configured = readConfiguredLibraryRoot()) options.libraryRoot = *configured;
  }
  if(!options.libraryRoot.empty()) {
    if(!openLibraryRoot(ui, options.libraryRoot)) {
      std::cerr << "Failed to open library: " << options.libraryRoot << "\n";
      return 1;
    }
  }
  if(!options.selectTitle.empty()) {
    for(const auto& note : ui.state.currentNotes()) {
      if(note.title.find(options.selectTitle) == std::string::npos) continue;
      selectNoteById(ui, note.id);
      break;
    }
  }
  if(options.paneMode) {
    ui.state.shell().paneMode = *options.paneMode == 0 ? ui::PaneMode::Editor
      : *options.paneMode == 1 ? ui::PaneMode::Viewer
      : *options.paneMode == 2 ? ui::PaneMode::Split
      : ui::PaneMode::Live;
  }
  if(!attachFromCli(ui, options.attachPath)) return 1;
  if(options.headless) return 0;

  if(!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  if(options.theme) ui::setThemeMode(*options.theme);

  SDL_Window* window = SDL_CreateWindow("micronotes", options.windowWidth, options.windowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if(!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if(!renderer) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_StartTextInput(window);
  TextRenderer text(renderer);
  float appliedScale = 0.0f;
  auto applyDisplayScale = [&]() {
    float scale = options.scale > 0.0f ? options.scale : SDL_GetWindowDisplayScale(window);
    if(scale <= 0.0f) scale = 1.0f;
    if(std::abs(scale - appliedScale) < 0.01f) return;
    appliedScale = scale;
    text.setDisplayScale(scale);
    // Layout stays in logical units; SDL scales it up, and glyph textures are
    // drawn at their own physical size so they stay sharp.
    SDL_SetRenderScale(renderer, scale, scale);
  };
  applyDisplayScale();
  if(inputDebugEnabled()) {
    std::cerr << "fonts source=\"" << text.fonts().sourceDescription() << "\""
              << " ready=" << text.fonts().ready() << "\n";
  }
  ImageCache images(renderer);
  SystemCursors cursors;
  if(!cursors.init()) {
    std::cerr << "SDL_CreateSystemCursor failed: " << SDL_GetError() << "\n";
  }
  auto updateCursor = [&](int width, int height) {
    cursors.apply(classifyCursor(text, ui, width, height));
  };

  auto autosaveWaitMs = [&]() -> int {
    if(!ui.state.hasLibrary() || !ui.editor.dirty() || ui.state.selection().noteId.empty()) return -1;
    const Uint64 now = SDL_GetTicks();
    const Uint64 next = std::max(ui.lastEdit + 1201, ui.lastAutosaveAttempt + 1001);
    if(now >= next) return 0;
    return std::clamp(static_cast<int>(next - now), 1, 1200);
  };

  if(!options.openOverlay.empty()) {
    const auto& which = options.openOverlay;
    if(which == "rename") beginRename(ui);
    else if(which == "tags") beginTagEdit(ui);
    else if(which == "new-folder") beginFolderCreate(ui);
    else if(which == "note-menu") openNoteMenu(ui, 420.0f, 200.0f);
    else if(which == "folder-menu") openFolderMenu(ui, 60.0f, 160.0f);
    else if(which == "delete-note") openDeleteNoteConfirm(ui);
    else if(which == "settings") openSettings(ui);
    else if(which == "shortcuts") openShortcutHelp(ui);
    else if(which == "command-palette") openCommandPalette(ui);
    else std::cerr << "unknown --open value: " << which << "\n";
  }

  if(!options.screenshotPath.empty()) {
    const int code = captureFrame(renderer, text, images, ui, options);
    cursors.destroy();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
  }

  bool running = true;
  bool needsDraw = true;
  while(running) {
    SDL_Event event;
    const int waitMs = autosaveWaitMs();
    const bool hasEvent = waitMs < 0 ? SDL_WaitEvent(&event) : SDL_WaitEventTimeout(&event, waitMs);
    if(hasEvent) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      do {
        needsDraw = true;
      if(event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if(event.type == SDL_EVENT_TEXT_INPUT) {
        handleText(ui, event.text.text);
      } else if(event.type == SDL_EVENT_KEY_DOWN) {
        handleKey(ui, event.key.key, event.key.scancode, event.key.mod);
      } else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        ui.mouseX = event.button.x;
        ui.mouseY = event.button.y;
        handleMouse(text, ui, event.button.x, event.button.y, event.button.button, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        ui.mouseX = event.button.x;
        ui.mouseY = event.button.y;
        handleMouseUp(ui, event.button.x, event.button.y, event.button.button, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_MOTION) {
        ui.mouseX = event.motion.x;
        ui.mouseY = event.motion.y;
        if(ui.overlays.active()) {
          ui.overlays.handleMotion(event.motion.x, event.motion.y);
          updateCursor(width, height);
        } else if(ui.draggingBlock) {
          ui.blockDropOffset = ui.livePage.dropOffsetAt(event.motion.y);
        } else if(ui.selectingEditorText) {
          if(ui.state.shell().paneMode == ui::PaneMode::Live) {
            ui.editor.selectRange(ui.editorSelectionAnchor, ui.livePage.offsetAt(event.motion.x, event.motion.y));
            ui.revealEditorCursor = true;
          } else {
            const AppLayout layout = computeLayout(ui.state.shell(), width, height);
            Rect editorRect = layout.content;
            if(ui.state.shell().paneMode == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
            const auto cursor = editorIndexAtPoint(text, ui, editorRect, event.motion.x, event.motion.y);
            ui.editor.selectRange(ui.editorSelectionAnchor, cursor);
            ui.revealEditorCursor = true;
          }
        } else if(ui.scrollDragTarget != ScrollDragTarget::None) {
          const AppLayout layout = computeLayout(ui.state.shell(), width, height);
          if(ui.scrollDragTarget == ScrollDragTarget::Live) {
            ui.livePage.setScroll(scrollFromThumbY(ui.livePage.pageRect(), event.motion.y, ui.scrollDragOffsetY, ui.livePage.maxScroll()));
          } else if(ui.scrollDragTarget == ScrollDragTarget::Editor) {
            Rect editorRect = layout.content;
            if(ui.state.shell().paneMode == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
            Rect writing {editorRect.x + 8, editorRect.y + 8, editorRect.w - 16, editorRect.h - 28};
            const int maxScroll = editorMaxScroll(text, ui, editorRect);
            ui.editorScroll = scrollFromThumbY(writing, event.motion.y, ui.scrollDragOffsetY, maxScroll);
            ui.revealEditorCursor = false;
          } else if(ui.scrollDragTarget == ScrollDragTarget::Viewer) {
            Rect viewerRect = layout.content;
            if(ui.state.shell().paneMode == ui::PaneMode::Split) {
              const float split = layout.content.w / 2.0f;
              viewerRect = {layout.content.x + split, layout.content.y, layout.content.w - split, layout.content.h};
            }
            Rect page {viewerRect.x + 8, viewerRect.y + 8, viewerRect.w - 16, viewerRect.h - 28};
            const int maxScroll = viewerMaxScroll(text, ui, viewerRect);
            ui.viewerScroll = scrollFromThumbY(page, event.motion.y, ui.scrollDragOffsetY, maxScroll);
          }
        } else if(ui.draggingNote || ui.draggingFolder) {
          const AppLayout layout = computeLayout(ui.state.shell(), width, height);
          const auto row = sidebarRowAt(ui, layout.sidebar, event.motion.x, event.motion.y);
          ui.sidebarDropRow = row && ui.sidebarRows[*row].kind == SidebarRow::Kind::Tree
                                ? row
                                : std::optional<std::size_t> {};
        } else if(ui.resizingSidebar) {
          ui.state.shell().sidebarWidth = std::clamp(static_cast<int>(event.motion.x), 150, std::max(150, width - 520));
        } else if(ui.resizingNotes) {
          const AppLayout layout = computeLayout(ui.state.shell(), width, height);
          ui.state.shell().noteListWidth = std::clamp(static_cast<int>(event.motion.x - layout.sidebar.w), 190, std::max(190, width - static_cast<int>(layout.sidebar.w) - 320));
        }
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL && ui.overlays.active()) {
        // An open overlay owns the wheel outright, or a long palette would
        // scroll the note behind it instead of itself.
        ui.overlays.handleWheel(event.wheel.y);
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL &&
                contains(computeLayout(ui.state.shell(), width, height).sidebar, ui.mouseX, ui.mouseY)) {
        // The tree can be taller than the window, so the pointer's column
        // decides where a wheel goes before the pane mode does.
        ui.sidebarScroll = std::clamp(ui.sidebarScroll - static_cast<int>(event.wheel.y * 48), 0, ui.sidebarMaxScroll);
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL && ui.state.shell().paneMode == ui::PaneMode::Live) {
        ui.livePage.setScroll(ui.livePage.scroll() - static_cast<int>(event.wheel.y * 60));
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL) {
        const AppLayout layout = computeLayout(ui.state.shell(), width, height);
        Rect editorRect = layout.content;
        Rect viewerRect = layout.content;
        bool wheelEditor = false;
        bool wheelViewer = false;
        if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
          wheelEditor = contains(editorRect, ui.mouseX, ui.mouseY) || ui.focus == FocusArea::Editor;
        } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
          wheelViewer = contains(viewerRect, ui.mouseX, ui.mouseY) || ui.focus == FocusArea::Viewer;
        } else {
          editorRect.w = layout.content.w / 2.0f;
          viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
          wheelEditor = contains(editorRect, ui.mouseX, ui.mouseY);
          wheelViewer = contains(viewerRect, ui.mouseX, ui.mouseY);
        }
        if(wheelViewer) {
          ui.viewerScroll = std::max(0, ui.viewerScroll - static_cast<int>(event.wheel.y * 42));
        } else if(wheelEditor) {
          ui.editorScroll = std::clamp(ui.editorScroll - static_cast<int>(event.wheel.y * 3), 0, editorMaxScroll(text, ui, editorRect));
          ui.revealEditorCursor = false;
        }
      } else if(event.type == SDL_EVENT_DROP_FILE) {
        if(event.drop.data) attachPathToEditor(ui, event.drop.data);
      } else if(event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        applyDisplayScale();
      } else if(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        if(ui.state.hasLibrary() && !ui.editor.dirty()) ui.state.refreshLibrary();
      }
      } while(SDL_PollEvent(&event));
    }
    const Uint64 now = SDL_GetTicks();
    if(ui.state.hasLibrary() && ui.editor.dirty() && !ui.state.selection().noteId.empty() &&
       now - ui.lastEdit > 1200 && now - ui.lastAutosaveAttempt > 1000) {
      ui.lastAutosaveAttempt = now;
      (void)saveCurrent(ui, true);
      needsDraw = true;
    }
    if(needsDraw) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      drawApp(renderer, text, images, ui, width, height);
      needsDraw = false;
    }
  }

  if(ui.state.hasLibrary() && ui.editor.dirty() && !ui.state.selection().noteId.empty()) (void)saveCurrent(ui, true);
  persistLibraryState(ui);
  SDL_StopTextInput(window);
  cursors.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}
