#include "CoreAliases.h"
#include "app/Application.h"

#include "app/PageView.h"
#include "app/Notes.h"
#include "app/InlineText.h"
#include "app/MarkdownBlocks.h"
#include "app/PageHeader.h"
#include "app/LivePage.h"
#include "app/ReadingPage.h"
#include "app/SessionState.h"
#include "app/Ribbon.h"
#include "app/Scroll.h"
#include "app/RightPanel.h"
#include "app/Chrome.h"
#include "app/EditorBlocks.h"
#include "app/Folds.h"
#include "app/FramePolicy.h"
#include "app/FrameTrace.h"
#include "app/Screenshot.h"
#include "app/RawPane.h"
#include "app/ContextMenus.h"
#include "app/SettingsDialog.h"
#include "app/TabStrip.h"
#include "app/WikiLinks.h"
#include "app/WindowChrome.h"
#include "app/Shell.h"
#include "app/SidebarModel.h"
#include "app/Sidebar.h"
#include "core/attachments/AttachmentService.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "doc/Fold.h"
#include "core/editor/MarkdownEditor.h"
#include "core/editor/SoftWrap.h"
#include "core/markdown/MarkdownParser.h"
#include "core/platform/DurableFile.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/render/FontResolver.h"
#include "core/render/TextTextureCache.h"
#include "core/editor/SingleLineView.h"
#include "core/platform/PathUtils.h"
#include "ui/AppState.h"
#include "ui/Draw.h"
#include "ui/FoldState.h"
#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/Overlay.h"
#include "ui/SearchScope.h"
#include "ui/ShellLayout.h"
#include "ui/Settings.h"
#include "ui/Outline.h"
#include "ui/TextUtil.h"
#include "ui/WikiLink.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/TreeModel.h"
#include "core/editor/TextField.h"

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
using micronotes::ui::ShellLayout;
using micronotes::ui::ShellLayoutInputs;
using micronotes::ui::computeShellLayout;
using micronotes::ui::TextRenderer;
using micronotes::ui::clipRect;
using micronotes::ui::contains;
using micronotes::ui::drawSelection;
using micronotes::ui::drawEmptyMessage;
using micronotes::ui::drawSectionLabel;
using micronotes::ui::drawSurface;
using micronotes::ui::drawTooltip;
using micronotes::ui::ellipsizeToWidth;
using micronotes::ui::fill;
using micronotes::ui::hLine;
using micronotes::ui::sdlRect;
using micronotes::ui::stroke;
using micronotes::ui::splitLines;
using micronotes::ui::ellipsize;
using micronotes::ui::isRemoteTarget;
using micronotes::ui::fileNameForMime;
using micronotes::ui::splitTags;
using micronotes::ui::joinTags;
using micronotes::ui::theme;

static void openDeleteNoteConfirm(UiRuntime& ui);
static void updateFindStatus(UiRuntime& ui);
// The shell's geometry, as a pure function of the window and the shell model.
// Block transforms arrive as one erase-and-insert, so they land on the editor's
// single undo stack instead of keeping state of their own.
static bool applyEdit(UiRuntime& ui, const doc::Edit& edit) {
  if(!edit.valid) return false;
  ui.editor.replaceRange(edit.start, edit.end, edit.text);
  if(edit.selects) ui.editor.selectRange(edit.anchor, edit.cursor);
  else ui.editor.moveCursor(edit.cursor);
  ui.markEdited();
  ui.revealEditorCursor = true;
  return true;
}

// Runs a transform against the buffer as it stands right now. Chaining these
// with `||` is safe: a transform only sees the buffer when the earlier ones
// declined to change it -- and a transform that did change it has moved the
// editor's revision on, so the next one in the chain is handed no partition
// rather than a stale one.
template <typename Transform>
static bool applyTransform(UiRuntime& ui, Transform&& transform) {
  return applyEdit(ui, transform(ui.editor.text(), ui.editor.cursor(), editorBlocks(ui)));
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

// The blocks a command applies to: the block selection when there is one,
// otherwise the block holding the caret. Both ends are carets, not indices.
static std::pair<std::size_t, std::size_t> blockSelectionCarets(const UiRuntime& ui) {
  if(!ui.blockSelectActive) return {ui.editor.cursor(), ui.editor.cursor()};
  return {std::min(ui.blockSelectAnchor, ui.blockSelectFocus),
          std::max(ui.blockSelectAnchor, ui.blockSelectFocus)};
}

static void selectBlockAtCursor(UiRuntime& ui) {
  const EditorBlocks blocks(ui);
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
  const EditorBlocks blocks(ui);
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
  if(applyEdit(ui, doc::turnBlocksInto(ui.editor.text(), from, to, kind, level, editorBlocks(ui)))) {
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
  if(!applyEdit(ui, doc::moveBlocks(ui.editor.text(), from, to, delta, editorBlocks(ui)))) return false;
  syncBlockSelectionToEdit(ui);
  ui.status = delta < 0 ? "Moved block up" : "Moved block down";
  return true;
}

// Folding changes what is on screen and never the file, so it goes nowhere near
// the editor or the undo stack.
static void toggleFoldAt(UiRuntime& ui, std::size_t caret) {
  const std::string& source = ui.editor.text();
  const EditorBlocks blocks(ui);
  const std::size_t index = doc::foldHeadFor(blocks, doc::blockIndexAt(blocks, std::min(caret, source.size())));
  if(index >= blocks.size()) {
    ui.status = "Nothing to fold here";
    return;
  }
  const bool folded = ui.folds.toggle(ui.state.selection().noteId, doc::foldKey(source, blocks[index]));
  // A section that just collapsed must not be left holding the caret. Only the
  // blocks it actually hides count: the caret further down the note stays put.
  const std::size_t end = doc::foldEnd(blocks, index);
  const std::size_t caretNow = ui.editor.cursor();
  if(folded && caretNow >= blocks[index].end() && caretNow < blocks[end - 1].end()) {
    ui.editor.moveCursor(blocks[index].contentEnd());
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
    if(applyEdit(ui, doc::duplicateBlocks(ui.editor.text(), from, to, editorBlocks(ui)))) {
      syncBlockSelectionToEdit(ui);
      ui.status = "Duplicated block";
    }
  } else if(id == "delete") {
    if(applyEdit(ui, doc::deleteBlocks(ui.editor.text(), from, to, editorBlocks(ui)))) {
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
  ui.markEdited();
  SDL_free(raw);
  return true;
}

static editor::TextField* focusedField(UiRuntime& ui) {
  switch(ui.focus) {
    case FocusArea::Search: return &ui.search;
    case FocusArea::Find: return &ui.find;
    case FocusArea::TagEditor: return &ui.tag;
    case FocusArea::RenameNote: return &ui.rename;
    case FocusArea::RenameFolder: return &ui.folderRename;
    default: return nullptr;
  }
}

static void syncFocusedInput(UiRuntime& ui) {
  if(ui.focus == FocusArea::Search) {
    ui.state.setSearch(ui.search.text(), ui.searchScope);
    selectNoteAt(ui, 0);
  } else if(ui.focus == FocusArea::Find) {
    updateFindStatus(ui);
  }
}

static bool pasteClipboardIntoInput(UiRuntime& ui) {
  auto* input = focusedField(ui);
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
  // Lands at the caret and replaces the selection, rather than being appended
  // to the end of the field regardless of where the user was working.
  input->editor.insert(raw);
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
  ui.markEdited();
  SDL_free(raw);
  return true;
}

static bool pastePrimarySelectionIntoInput(UiRuntime& ui) {
  auto* input = focusedField(ui);
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
  input->editor.insert(raw);
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
  overlay.value.beginWith(joinTags(note->metadata.tags));
  overlay.placeholder = "space separated";
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void saveTags(UiRuntime& ui) {
  if(ui.state.updateSelectedTags(splitTags(ui.tag.text()))) {
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
  overlay.value.beginWith(note->metadata.title.empty() ? note->item.title : note->metadata.title);
  overlay.placeholder = "Note title";
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void saveRename(UiRuntime& ui) {
  if(ui.rename.empty()) {
    ui.status = "Rename needs a title";
    return;
  }
  invalidateWikiNotes(ui);
  if(ui.state.renameSelectedNote(ui.rename.text())) {
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
  overlay.value.beginWith("Notebook");
  overlay.placeholder = "Notebook name";
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
  overlay.value.beginWith(ui.state.selection().folder.generic_string());
  overlay.placeholder = "Notebook name";
  overlay.hint = "Enter to save, Esc to cancel";
  ui.overlays.open(std::move(overlay));
}

static void saveFolderRename(UiRuntime& ui) {
  if(ui.folderRename.empty()) {
    ui.status = "Notebook name is required";
    return;
  }
  const bool creating = ui.creatingFolder;
  const bool saved = creating ? ui.state.createFolder(ui.folderRename.text()) : ui.state.renameSelectedFolder(ui.folderRename.text());
  if(saved) {
    ui.creatingFolder = false;
    ui.focus = FocusArea::Folders;
    ui.status = creating ? "Created notebook" : "Saved notebook";
  } else {
    ui.status = "Notebook change failed";
  }
}

static void deleteSelected(UiRuntime& ui) {
  invalidateWikiNotes(ui);
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

static void setPaneMode(UiRuntime& ui, ui::PaneMode mode) {
  ui.clearBlockSelection();
  ui.state.workspace().setPaneMode(mode);
  ui.focus = mode == ui::PaneMode::Viewer ? FocusArea::Viewer : FocusArea::Editor;
  ui.revealEditorCursor = true;
  ui.status = paneModeName(mode);
}

static void cyclePaneMode(UiRuntime& ui) {
  switch(ui.state.workspace().paneMode()) {
    case ui::PaneMode::Live: setPaneMode(ui, ui::PaneMode::Editor); break;
    case ui::PaneMode::Editor: setPaneMode(ui, ui::PaneMode::Viewer); break;
    case ui::PaneMode::Viewer: setPaneMode(ui, ui::PaneMode::Split); break;
    case ui::PaneMode::Split: setPaneMode(ui, ui::PaneMode::Live); break;
  }
}

static void updateFindStatus(UiRuntime& ui) {
  const std::string& needle = ui.find.text();
  if(needle.empty()) {
    ui.status = "Find in note";
    return;
  }
  std::size_t count = 0;
  std::size_t pos = ui.editor.text().find(needle);
  while(pos != std::string::npos) {
    ++count;
    pos = ui.editor.text().find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
  ui.status = std::to_string(count) + " matches in note";
}

static void performAction(UiRuntime& ui, UiAction action) {
  switch(action) {
    case UiAction::Refresh:
      invalidateWikiNotes(ui);
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
      ui.state.workspace().setPaneMode(ui::PaneMode::Editor);
      ui.focus = FocusArea::Editor;
      break;
    case UiAction::PaneViewer:
      ui.state.workspace().setPaneMode(ui::PaneMode::Viewer);
      ui.focus = FocusArea::Viewer;
      break;
    case UiAction::PaneSplit:
      ui.state.workspace().setPaneMode(ui::PaneMode::Split);
      ui.focus = FocusArea::Editor;
      break;
  }
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

// One wheel notch scrolls three lines in the editor and roughly three lines'
// worth of pixels in the viewer, matching the platform convention.
// Lines moved by PageUp/PageDown. A fixed value rather than the visible line
// count because handleKey has no layout in scope; it matches a typical viewport.









// A hidden panel has no edge to grab: its width is zero, so its right edge sits
// on top of the next panel's left one and dragging there would resize a panel
// nobody can see.
static bool isResizeGutter(const ShellLayout& layout, float x, float y) {
  if(y < layout.sidebar.y || y > layout.sidebar.y + layout.sidebar.h) return false;
  const auto nearEdge = [&](const Rect& panel) {
    return !ui::empty(panel) && std::abs(x - (panel.x + panel.w)) <= ui::kResizeGutterInflate + 1.0f;
  };
  return nearEdge(layout.sidebar) ||
         (!ui::empty(layout.rightPanel) &&
          std::abs(x - layout.rightPanel.x) <= ui::kResizeGutterInflate + 1.0f);
}

// One place where a sidebar row turns into a selection, so a click, an arrow
// key and a drop can never disagree about what selecting a row means.
static void activateSidebarRow(UiRuntime& ui, const SidebarRow& row, bool expandFolder) {
  if(row.kind == SidebarRow::Kind::SearchResult) {
    selectNoteById(ui, row.noteId);
    return;
  }
  if(row.kind == SidebarRow::Kind::Tag) {
    selectTag(ui, row.tag);
    return;
  }
  if(row.kind != SidebarRow::Kind::Tree) return;
  if(row.tree.kind == ui::TreeRowKind::Note) {
    selectNoteById(ui, row.tree.noteId);
    // Opening a note from the tree moves the context to its folder too, so the
    // breadcrumb agrees with what is on screen. A search owns the row list
    // while it is running, so it is left alone.
    if(ui.search.empty() && ui.state.selection().noteId == row.tree.noteId) {
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


static bool scrollbarHit(Rect viewport, int scroll, int maxScroll, float x, float y) {
  if(maxScroll <= 0) return false;
  return contains(scrollbarHitRect(scrollbarThumb(viewport, scroll, maxScroll)), x, y);
}

static CursorKind classifyCursor(TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(ui.resizingSidebar) return CursorKind::ResizeHorizontal;
  if(ui.scrollDragTarget != ScrollDragTarget::None) return CursorKind::ResizeVertical;

  const float x = ui.mouseX;
  const float y = ui.mouseY;
  const ShellLayout layout = shellLayout(ui, width, height);
  if(isResizeGutter(layout, x, y)) return CursorKind::ResizeHorizontal;
  if(contains(layout.titleBar, x, y)) {
    if(contains(ui.favoriteButton, x, y)) return CursorKind::Pointer;
    for(const auto& box : ui.windowButtons) {
      if(contains(box, x, y)) return CursorKind::Pointer;
    }
    for(const auto& [rect, folder] : ui.crumbs) {
      (void)folder;
      if(contains(rect, x, y)) return CursorKind::Pointer;
    }
  }

  if(ui.overlays.active()) return CursorKind::Pointer;

  if(contains(layout.ribbon, x, y)) {
    return ribbonHasControlAt(ui, layout.ribbon, x, y) ? CursorKind::Pointer : CursorKind::Default;
  }

  if(contains(layout.sidebar, x, y)) {
    const Rect search = searchBoxRect(layout.sidebar);
    if(contains(search, x, y)) {
      return contains(ui.searchScopeToggle, x, y) ? CursorKind::Pointer : CursorKind::Text;
    }
    if(sidebarRowAt(ui, sidebarListRect(layout.sidebar), x, y)) return CursorKind::Pointer;
    return CursorKind::Default;
  }

  if(!contains(layout.content, x, y)) return CursorKind::Default;

  if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
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
  if(ui.state.workspace().paneMode() == ui::PaneMode::Editor) {
    hasEditor = true;
  } else if(ui.state.workspace().paneMode() == ui::PaneMode::Viewer) {
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
    const Rect page = ui::pageRectIn(viewerRect);
    if(scrollbarHit(page, ui.viewerScroll, ui.viewerMaxScroll, x, y)) {
      return CursorKind::Pointer;
    }
    for(const auto& link : ui.linkRegions) {
      if(contains(link.rect, x, y)) return CursorKind::Pointer;
    }
  }

  return CursorKind::Default;
}

// One frame of the whole window. Every surface it calls is timed separately:
// before that, `page.draw` was the only instrumented part of a frame, so a
// frame whose cost was the sidebar or the chrome showed up as time that went
// nowhere.
static void drawApp(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, int width, int height) {
  ScopedFrame frame;
  SDL_SetRenderDrawColor(renderer, theme().appBg.r, theme().appBg.g, theme().appBg.b, theme().appBg.a);
  SDL_RenderClear(renderer);

  const ShellLayout layout = shellLayout(ui, width, height);
  ui.linkRegions.clear();
  ui.buttonRegions.clear();
  // Set by whichever surface draws the header this frame, and by none of them
  // in raw mode -- so it is cleared here rather than left pointing at where the
  // title was the last time a pane that has one was showing.
  ui.headerTitleRect = {};
  // Cleared here and set by whichever surface the pointer turns out to be over,
  // so a frame can never end up with two tooltips resolved.
  ui.tooltip = {};

  // First, whatever is or is not open behind it: it carries the window controls.
  {
    const perf::ScopeTimer timer("shell.title_bar");
    drawTitleBar(renderer, text, ui, layout.titleBar);
  }

  // The rail, then whatever is beside it. Drawn before the panels because it is
  // the one column that is always there: everything else lays out against it.
  {
    const perf::ScopeTimer timer("shell.ribbon");
    drawRibbon(renderer, text, ui, layout.ribbon);
  }

  // A hidden panel is zero wide, and its rule would land on the edge of
  // whatever took its place.
  if(!ui::empty(layout.sidebar)) {
    const perf::ScopeTimer timer("shell.sidebar");
    drawSidebar(renderer, text, ui, layout.sidebar);
    fill(renderer, {layout.sidebar.x + layout.sidebar.w, layout.sidebar.y, 1, layout.sidebar.h}, theme().hairline);
  }
  if(!ui::empty(layout.rightPanel)) {
    const perf::ScopeTimer timer("shell.right_panel");
    drawRightPanel(renderer, text, ui, layout.rightPanel);
  }
  if(!ui::empty(layout.tabs)) {
    const perf::ScopeTimer timer("shell.tab_strip");
    drawTabStrip(renderer, text, ui, layout.tabs);
  }
  if(!ui.state.hasLibrary()) {
    fill(renderer, layout.content, theme().editorBg);
    // The one screen someone can arrive at knowing nothing, so it says what
    // the app is for before it says which key to press.
    drawEmptyMessage(text, "Open a folder of notes",
                     "micronotes reads and writes plain Markdown files in one local folder. Nothing leaves your disk.",
                     layout.content.x + 18.0f, layout.content.y + 40.0f, layout.content.w - 36.0f,
                     ui::keysFor(ui::ActionId::Settings) + "  Settings          or start with  --library <path>");
  } else if(ui.state.selection().noteId.empty()) {
    fill(renderer, layout.content, theme().editorBg);
    drawEmptyMessage(text, "Nothing open", "Pick a note from the sidebar, or start a new one.",
                     layout.content.x + 18.0f, layout.content.y + 40.0f, layout.content.w - 36.0f,
                     ui::keysFor(ui::ActionId::GoToNote) + "  go to note          " + ui::keysFor(ui::ActionId::NewNote) +
                     "  new note          " + ui::keysFor(ui::ActionId::Shortcuts) + "  every shortcut");
  } else {
    const perf::ScopeTimer timer("shell.content");
    const Rect content = layout.content;
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      drawLive(renderer, text, images, ui, content);
    } else if(ui.state.workspace().paneMode() == ui::PaneMode::Editor) {
      drawEditor(renderer, text, ui, content);
    } else if(ui.state.workspace().paneMode() == ui::PaneMode::Viewer) {
      drawReading(renderer, text, images, ui, content);
    } else {
      const float split = content.w / 2.0f;
      drawEditor(renderer, text, ui, {content.x, content.y, split, content.h});
      fill(renderer, {content.x + split, content.y, 1, content.h}, theme().hairline);
      drawReading(renderer, text, images, ui, {content.x + split, content.y, content.w - split, content.h});
    }
  }
  {
    const perf::ScopeTimer timer("shell.status");
    fill(renderer, layout.status, theme().statusBg);
    fill(renderer, {layout.status.x, layout.status.y, layout.status.w, 1}, theme().hairline);
    drawStatus(renderer, text, ui, layout.status);
  }
  // An open overlay is a conversation; a tooltip about what is behind it would
  // be answering a question nobody is asking any more.
  if(ui.overlays.active()) ui.tooltip = {};
  {
    const perf::ScopeTimer timer("shell.overlays");
    ui.overlays.draw(renderer, text, width, height);
    // Last, so nothing paints over it.
    drawTooltip(renderer, text, ui.tooltip, {0, 0, static_cast<float>(width), static_cast<float>(height)});
  }
  // The frame's work ends here. With vsync on, the present below blocks until
  // the display is ready, so charging that wait to the frame reports the refresh
  // interval as if it were the app's cost.
  frame.markWorkDone();
  {
    const perf::ScopeTimer timer("shell.present");
    SDL_RenderPresent(renderer);
  }
}

static int captureFrame(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, const ApplicationOptions& options) {
  return captureWindowToFile(renderer, options.screenshotPath, options.windowWidth, options.windowHeight,
                             [&](int width, int height) { drawApp(renderer, text, images, ui, width, height); });
}

// The block types, as menu rows. One table feeds the slash menu, the turn-into
// menu and the block menu, so a new block type appears in all three at once.
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
  const EditorBlocks blocks(ui);
  const std::size_t head = doc::foldHeadFor(blocks, doc::blockIndexAt(blocks, ui.editor.cursor()));
  const bool folds = head < blocks.size();
  const bool folded = folds && ui.folds.folded(ui.state.selection().noteId, doc::foldKey(ui.editor.text(), blocks[head]));
  overlay.items = {
    {"turn", "Turn into", "", ui::keysFor(ui::ActionId::TurnInto), true, false},
    {"duplicate", "Duplicate", "", ui::keysFor(ui::ActionId::DuplicateBlock), true, false},
    {"fold", folded ? "Unfold" : "Fold", "", ui::keysFor(ui::ActionId::Fold), folds, false},
    {"move-up", "Move up", "", ui::keysFor(ui::ActionId::MoveBlockUp), true, false},
    {"move-down", "Move down", "", ui::keysFor(ui::ActionId::MoveBlockDown), true, false},
    {"delete", "Delete", "", ui::keysFor(ui::ActionId::DeleteBlock), true, true},
  };
  ui.overlays.open(std::move(overlay));
}

// `slashStart` is the "/" the user typed; committing erases [slashStart, caret)
// before the block transform runs.
static void openSlashMenu(UiRuntime& ui, std::size_t slashStart) {
  ui.slashStart = slashStart;
  ui.slashInserts = false;
  ui.clearBlockSelection();
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
    if(entry && applyEdit(ui, doc::insertBlockAfter(ui.editor.text(), ui.slashAfterBlock, entry->kind, entry->level,
                                             editorBlocks(ui)))) {
      ui.status = entry->label;
    }
    return;
  }
  const std::size_t caret = ui.editor.cursor();
  const std::size_t start = std::min(ui.slashStart, caret);
  if(start < caret) {
    ui.editor.replaceRange(start, caret, "");
    ui.markEdited();
  }
  performBlockCommand(ui, itemId);
}

// The palette is a view of ui::actionSpecs(), not a second list beside it.
// It used to be its own table, and the rule written above it -- that an action
// reachable only by shortcut belongs in both -- was enforced by nobody.
static void performCommand(UiRuntime& ui, const std::string& id);
static void focusFindInNote(UiRuntime& ui);
static void focusSearchAllNotes(UiRuntime& ui);
static void openDeleteFolderConfirm(UiRuntime& ui);

static void openCommandPalette(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "command-palette";
  overlay.title = "Commands";
  overlay.filterable = true;
  overlay.placeholder = "Type a command";
  overlay.hint = "Enter run   Esc cancel";
  overlay.width = 460.0f;
  const bool hasNote = !ui.state.selection().noteId.empty();
  for(const auto& spec : ui::actionSpecs()) {
    if(!spec.inPalette) continue;
    // Commands that need something selected are listed but refused, rather
    // than hidden: a palette that changes shape is a palette you cannot learn.
    overlay.items.push_back({std::string(spec.name), std::string(spec.label), "",
                             ui::acceleratorText(spec), !spec.needsNote || hasNote, false});
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
    const auto folder = note.folder.generic_string();
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
  overlay.value.beginWith(note->metadata.icon);
  overlay.placeholder = "One emoji";
  overlay.hint = "Enter save   Esc cancel   empty removes the icon";
  ui.overlays.open(std::move(overlay));
}

// `~` for the home directory, as everything else that prints a path does.
// Settings are a list of what can change and what it is now; each row opens the
// list of its own values. A list overlay rather than a panel of widgets,
// because the keyboard, the filter and the dismissal rules are then the ones
// already learnt from every other overlay in the app.
static void openLibraryPrompt(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::TextPrompt;
  overlay.id = "settings-library";
  overlay.title = "Library folder";
  overlay.value.beginWith(ui.state.hasLibrary() ? ui::displayPath(ui.state.libraryRoot()) : std::string {});
  overlay.placeholder = "~/Notes";
  overlay.hint = "Enter open   Esc cancel   a folder that is not there is created";
  overlay.width = 520.0f;
  ui.overlays.open(std::move(overlay));
}

// Opens a different library without restarting. The one being left is written
// out first, so its open note, favorites and folds go with it rather than
// following the user into the new one.
void switchLibrary(UiRuntime& ui, const std::string& typed) {
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
      ui.status = "Could not open " + ui::displayPath(root);
      return;
    }
  } catch(const std::exception& error) {
    ui.status = "Could not open " + ui::displayPath(root) + ": " + error.what();
    return;
  }
  writeConfiguredLibraryRoot(root);
  ui.status = "Opened " + ui::displayPath(root);
}

// Every binding the shell has, grouped, from the one table that also feeds the
// palette and the key handler. A row with no keys is a section heading: it is
// listed as a disabled item, so the arrows step over it and Enter cannot land
// on it.

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
  for(int i = 0; i < static_cast<int>(ui::ActionSection::Count); ++i) {
    const auto section = static_cast<ui::ActionSection>(i);
    std::vector<ui::OverlayItem> rows;
    for(const auto& spec : ui::actionSpecs()) {
      if(spec.section != section) continue;
      const auto keys = ui::acceleratorText(spec);
      if(keys.empty()) continue;
      rows.push_back({"", std::string(spec.label), "", keys, true, false});
    }
    for(const auto& row : ui::helpRows()) {
      if(row.section != section) continue;
      rows.push_back({"", std::string(row.what), "", std::string(row.keys), true, false});
    }
    if(rows.empty()) continue;
    overlay.items.push_back({"", std::string(ui::sectionLabel(section)), "", "", false, false});
    for(auto& row : rows) overlay.items.push_back(std::move(row));
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
  const EditorBlocks blocks(ui);
  const auto& first = blocks[doc::blockIndexAt(blocks, std::min(from, source.size()))];
  const auto& last = blocks[doc::blockIndexAt(blocks, std::min(to, source.size()))];
  std::string moved = source.substr(first.start, last.end() - first.start);
  while(!moved.empty() && moved.back() == '\n') moved.pop_back();
  if(moved.empty()) {
    ui.status = "Nothing to move";
    return;
  }
  if(!ui.state.appendToNote(target->id, moved)) {
    ui.status = "Move failed";
    return;
  }
  if(applyEdit(ui, doc::deleteBlocks(source, from, to, editorBlocks(ui)))) {
    ui.clearBlockSelection();
    ui.status = "Moved blocks to " + target->title;
  }
}

// Focusing a search field is an action like any other, so the key and the
// palette row run the same code rather than two copies that drift.
static void focusFindInNote(UiRuntime& ui) {
  ui.focus = FocusArea::Find;
  ui.find.editor.selectAll();
  updateFindStatus(ui);
}

static void focusSearchAllNotes(UiRuntime& ui) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.focus = FocusArea::Search;
  ui.search.editor.selectAll();
  ui.state.setSearch(ui.search.text(), ui.searchScope);
  ui.status = "Search all notes";
}

static void performCommand(UiRuntime& ui, const std::string& id) {
  if(id == "jump") openNotePalette(ui, "jump-note", "Go to note");
  else if(id == "command-palette") openCommandPalette(ui);
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
    invalidateWikiNotes(ui);
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  }
  else if(id == "pane-live") setPaneMode(ui, ui::PaneMode::Live);
  else if(id == "pane-raw") setPaneMode(ui, ui::PaneMode::Editor);
  else if(id == "pane-reading") setPaneMode(ui, ui::PaneMode::Viewer);
  else if(id == "pane-split") setPaneMode(ui, ui::PaneMode::Split);
  else if(id == "cycle-pane") cyclePaneMode(ui);
  else if(id == "find") focusFindInNote(ui);
  else if(id == "search") focusSearchAllNotes(ui);
  else if(id == "toggle-sidebar") togglePanel(ui, &ui::WorkspaceModel::sidebarVisible, "Sidebar");
  else if(id == "toggle-right") togglePanel(ui, &ui::WorkspaceModel::rightPanelVisible, "Outline panel");
  else if(id == "cycle-right") cycleRightPanel(ui);
  else if(id == "next-tab") stepTab(ui, 1);
  else if(id == "previous-tab") stepTab(ui, -1);
  else if(id == "close-tab") closeActiveTab(ui);
  else if(id == "new-tab") openNotePalette(ui, "jump-note-new", "Open in a new tab");
  else if(id == "pin-tab") {
    auto& workspace = ui.state.workspace();
    if(auto* tab = workspace.activeTab_()) {
      tab->pinned = !tab->pinned;
      ui.status = tab->pinned ? "Tab pinned" : "Tab unpinned";
    }
  }
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
    ui.rename.beginWith(result.value, false);
    saveRename(ui);
  } else if(result.overlayId == "tags") {
    ui.tag.beginWith(result.value, false);
    saveTags(ui);
  } else if(result.overlayId == "folder-name") {
    ui.folderRename.beginWith(result.value, false);
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
  } else if(result.overlayId == "wiki-menu") {
    commitWikiMenu(ui, result.itemId, result.value);
  } else if(result.overlayId == "command-palette") {
    performCommand(ui, result.itemId);
  } else if(result.overlayId == "jump-note-new") {
    if(saveCurrent(ui, true)) {
      ui.state.selectNote(result.itemId, true);
      loadSelectedIntoEditor(ui);
    }
  } else if(result.overlayId == "jump-note") {
    selectNoteById(ui, result.itemId);
    if(const auto note = ui.state.findNote(result.itemId)) {
      const auto folder = note->folder;
      ui.search.reset();
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

// Scrolls whichever of the raw editor and the reading view the pointer is over.
// The live surface does its own scrolling; this is the older pair.
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

static void handleText(UiRuntime& ui, const char* input) {
  if(!input) return;
  if(ui.overlays.active()) {
    ui.overlays.handleText(input);
    return;
  }
  if(auto* field = focusedField(ui)) {
    // insert() replaces the selection, so a select-all followed by a keystroke
    // overwrites without any separate "all selected" flag to keep in step.
    field->editor.insert(input);
    syncFocusedInput(ui);
  } else if(ui.focus == FocusArea::Editor) {
    // Typing is text editing, so it takes the caret back from a block selection
    // rather than replacing whole blocks with a character.
    ui.clearBlockSelection();
    ui.editor.insert(input);
    // "[] " only becomes a real task marker once the space lands, so the check
    // is cheap and runs at most once per typed space.
    if(std::string_view(input).find(' ') != std::string_view::npos) {
      applyTransform(ui, doc::applyMarkdownShortcut);
    }
    ui.markEdited();
    ui.revealEditorCursor = true;
    // "/" opens the block inserter, but only where a block could start: mid-word
    // slashes belong to paths and URLs.
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live && std::string_view(input) == "/") {
      const std::size_t slash = ui.editor.cursor() - 1;
      const char before = slash == 0 ? '\n' : ui.editor.text()[slash - 1];
      if(before == '\n' || before == ' ' || before == '\t') openSlashMenu(ui, slash);
    }
    // The second "[" of a "[[" offers the notes it could mean. A single bracket
    // is left alone: it is how every ordinary link and every task marker starts.
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live && std::string_view(input) == "[") {
      const std::size_t bracket = ui.editor.cursor() - 1;
      if(bracket > 0 && ui.editor.text()[bracket - 1] == '[') openWikiMenu(ui, bracket - 1);
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
              << " field_selection=" << (focusedField(ui) != nullptr && focusedField(ui)->editor.hasSelection())
              << "\n";
  }
  // Actions whose meaning does not depend on where the caret is are dispatched
  // straight from the binding table, so the chord lives in exactly one place.
  // The chain below is what is left to move: those branches read the focus, the
  // selection or the pane before deciding what the key meant, and each has to
  // be untangled before it can join this list.
  static constexpr ui::ActionId kBoundHere[] = {
    ui::ActionId::NextTab,
    ui::ActionId::PreviousTab,
    ui::ActionId::CloseTab,
    ui::ActionId::OpenInNewTab,
    ui::ActionId::ToggleSidebar,
    ui::ActionId::ToggleRightPanel,
    ui::ActionId::CycleRightPanel,
  };
  const ui::KeyChord pressed {key, ctrl, shift, alt};
  if(const auto* bound = ui::findActionForChord(pressed)) {
    if(std::find(std::begin(kBoundHere), std::end(kBoundHere), bound->id) != std::end(kBoundHere)) {
      performCommand(ui, std::string(bound->name));
      return;
    }
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
    invalidateWikiNotes(ui);
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
    else if(auto* field = focusedField(ui)) {
      field->editor.selectAll();
      if(field->editor.hasSelection()) SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
    }
  } else if(shortcut(SDLK_C, SDL_SCANCODE_C)) {
    if(ui.focus == FocusArea::Editor && ui.blockSelectActive) {
      const auto [from, to] = blockSelectionCarets(ui);
      const EditorBlocks blocks(ui);
      const std::size_t start = blocks[doc::blockIndexAt(blocks, from)].start;
      const std::size_t end = blocks[doc::blockIndexAt(blocks, to)].end();
      ui.status = setClipboardText(std::string_view(ui.editor.text()).substr(start, end - start))
                    ? "Copied block" : "Copy failed: " + std::string(SDL_GetError());
    } else if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      ui.status = setClipboardText(ui.editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    } else if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
      // Copies the actual selected range. It used to copy the whole field,
      // because the whole field was the only range that could be selected.
      ui.status = setClipboardText(field->editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_X, SDL_SCANCODE_X)) {
    if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      const bool copied = setClipboardText(ui.editor.selectedText());
      ui.editor.eraseSelection();
      ui.markEdited();
      ui.revealEditorCursor = true;
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    } else if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
      const bool copied = setClipboardText(field->editor.selectedText());
      field->editor.eraseSelection();
      syncFocusedInput(ui);
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_Z, SDL_SCANCODE_Z)) {
    // Resolved through `shortcut`, which also accepts the scancode, so Ctrl+Z
    // still works on a layout where the Z key does not produce 'z'.
    if(auto* field = focusedField(ui)) {
      if(field->editor.undo()) syncFocusedInput(ui);
    } else if(ui.focus == FocusArea::Editor && ui.editor.undo()) {
      ui.markEdited();
      ui.revealEditorCursor = true;
      ui.status = "Undo";
    }
  } else if(shortcut(SDLK_Y, SDL_SCANCODE_Y)) {
    if(auto* field = focusedField(ui)) {
      if(field->editor.redo()) syncFocusedInput(ui);
    } else if(ui.focus == FocusArea::Editor && ui.editor.redo()) {
      ui.markEdited();
      ui.revealEditorCursor = true;
      ui.status = "Redo";
    }
  } else if(shortcut(SDLK_V, SDL_SCANCODE_V)) {
    if(focusedField(ui)) pasteClipboardIntoInput(ui);
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
    focusSearchAllNotes(ui);
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F)) {
    focusFindInNote(ui);
  } else if(key == SDLK_ESCAPE) {
    if(ui.focus == FocusArea::Search && !ui.search.empty()) {
      ui.search.reset();
      ui.state.setSearch("", ui.searchScope);
    }
    if(ui.focus == FocusArea::Find) ui.find.reset();
    ui.creatingFolder = false;
    // In the live surface Esc steps out of the text and selects the block
    // itself; a second Esc puts the caret back.
    if(ui.focus == FocusArea::Editor && ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      if(ui.blockSelectActive) ui.clearBlockSelection();
      else selectBlockAtCursor(ui);
    }
    ui.focus = FocusArea::Editor;
  } else if(ui.focus == FocusArea::Search && (key == SDLK_DOWN || key == SDLK_UP)) {
    // The results are sidebar rows now, so walking them is the tree cursor: the
    // field keeps the typing and the list keeps the selection. A single-line
    // field has nothing else to do with Up and Down.
    moveTreeCursor(ui, key == SDLK_DOWN ? 1 : -1);
  } else if(auto* field = focusedField(ui)) {
    // Enter is the only key whose meaning depends on which field this is;
    // everything else -- arrows, word motion, Home/End, Backspace, Delete,
    // Ctrl+Z/Y -- is the same for all five and lives in one place.
    if(key == SDLK_RETURN) {
      switch(ui.focus) {
        case FocusArea::TagEditor: saveTags(ui); break;
        case FocusArea::RenameNote: saveRename(ui); break;
        case FocusArea::RenameFolder: saveFolderRename(ui); break;
        default: ui.focus = FocusArea::Editor; break;
      }
    } else {
      const auto result = editor::applyKeyToField(*field, key, ctrl, shift);
      if(result == editor::FieldKeyResult::Changed) syncFocusedInput(ui);
      else if(result == editor::FieldKeyResult::Moved && field->editor.hasSelection()) {
        SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
      }
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
      const EditorBlocks blocks(ui);
      const auto content = blocks[doc::blockIndexAt(blocks, blockSelectionCarets(ui).first)].contentStart();
      ui.clearBlockSelection();
      ui.editor.moveCursor(content);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_LEFT || key == SDLK_RIGHT) {
      ui.clearBlockSelection();
    }
  } else if(ui.focus == FocusArea::Editor) {
    const bool live = ui.state.workspace().paneMode() == ui::PaneMode::Live;
    // One visual row up or down: the live surface wraps, the raw editor does not.
    const auto rowStep = [&](int rows) {
      return live ? ui.livePage.rowRelative(ui.editor.cursor(), rows) : ui.editor.cursor();
    };
    if(key == SDLK_BACKSPACE) {
      if(ctrl) ui.editor.erasePreviousWord();
      // Against a block's first character, Backspace strips the block's marker
      // before it starts eating the block above.
      else if(ui.editor.hasSelection() || !applyTransform(ui, doc::outdentOrUnwrap)) ui.editor.erasePrevious();
      ui.markEdited();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DELETE) {
      if(ctrl) ui.editor.eraseNextWord();
      else ui.editor.eraseNext();
      ui.markEdited();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      if(ctrl) {
        if(!applyTransform(ui, doc::toggleTodo)) ui.status = "No task to toggle here";
      } else if(ui.editor.hasSelection() ||
                (!applyTransform(ui, doc::closeFence) && !applyTransform(ui, doc::continueList))) {
        ui.editor.insert("\n");
        ui.markEdited();
        ui.revealEditorCursor = true;
      }
    } else if(key == SDLK_TAB) {
      if(shift) {
        applyTransform(ui, doc::outdent);
      } else if(!applyTransform(ui, doc::indent)) {
        ui.editor.insert("  ");
        ui.markEdited();
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
  const ShellLayout layout = shellLayout(ui, width, height);

  // The rail first. It owns its whole column, so a click that lands between two
  // of its buttons is swallowed rather than falling through to the sidebar
  // behind it -- which is not behind it at all, but next to it.
  if(contains(layout.ribbon, x, y)) {
    if(button != SDL_BUTTON_LEFT) return;
    if(const auto action = handleRibbonClick(ui, layout.ribbon, x, y)) {
      if(const auto* spec = ui::findAction(*action)) performCommand(ui, std::string(spec->name));
    }
    return;
  }

  if(!ui::empty(layout.tabs) &&
     handleTabStripClick(ui, layout.tabs, x, y, button, (SDL_GetModState() & SDL_KMOD_CTRL) != 0)) {
    return;
  }

  // The right panel owns everything inside it, including its own background:
  // without that, a click between two outline rows would fall through to the
  // page and move the caret somewhere the reader never pointed at.
  if(button == SDL_BUTTON_LEFT && !ui::empty(layout.rightPanel) &&
     handleRightPanelClick(ui, text, layout.rightPanel, x, y)) {
    return;
  }

  if(button == SDL_BUTTON_MIDDLE) {
    if(contains(searchBoxRect(layout.sidebar), x, y)) {
      ui.focus = FocusArea::Search;
      // Middle-click pastes at the point pressed, like every other X11 text
      // field, rather than always at the end of the string.
      ui.search.editor.moveCursor(fieldOffsetAtX(text, ui.search, searchTextRect(layout.sidebar, text), x));
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y) && ui.state.workspace().paneMode() == ui::PaneMode::Live) {
      ui.focus = FocusArea::Editor;
      ui.editor.moveCursor(ui.livePage.offsetAt(x, y));
      ui.revealEditorCursor = true;
      ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y)) {
      Rect editorRect = layout.content;
      bool editorAtPoint = ui.state.workspace().paneMode() == ui::PaneMode::Editor;
      if(ui.state.workspace().paneMode() == ui::PaneMode::Split) {
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
    if(auto* field = focusedField(ui)) {
      // The search box is the one field drawn in a pane, so a middle click
      // inside it drops the caret where it landed before pasting.
      if(ui.focus == FocusArea::Search) {
        const Rect fieldRect = searchTextRect(layout.sidebar, text);
        if(contains(fieldRect, x, y)) {
          field->editor.moveCursor(fieldOffsetAtX(text, *field, fieldRect, x));
        }
      }
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
    }
    return;
  }

  if(pageHeaderClickAway(ui, x, y) == TitleEdit::Kept) saveRename(ui);

  // The inline title, before the page beneath it: a click on the note's name is
  // a rename, not a caret placed in the first paragraph. Not gated on the pane
  // mode -- the title's rect is cleared every frame and set only by a surface
  // that actually drew one, so a mode with no header has nothing to hit.
  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && handlePageHeaderClick(ui, x, y)) {
    return;
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && ui.state.workspace().paneMode() == ui::PaneMode::Live) {
    const int maxScroll = ui.livePage.maxScroll();
    const auto thumb = scrollbarThumb(ui.livePage.pageRect(), ui.livePage.scroll(), maxScroll);
    if(maxScroll > 0 && contains(scrollbarHitRect(thumb), x, y)) {
      ui.scrollDragTarget = ScrollDragTarget::Live;
      ui.scrollDragOffsetY = y - thumb.y;
      ui.focus = FocusArea::Editor;
      return;
    }
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y) && ui.state.workspace().paneMode() != ui::PaneMode::Live) {
    Rect editorRect = layout.content;
    Rect viewerRect = layout.content;
    bool hasEditor = false;
    bool hasViewer = false;
    if(ui.state.workspace().paneMode() == ui::PaneMode::Editor) {
      hasEditor = true;
    } else if(ui.state.workspace().paneMode() == ui::PaneMode::Viewer) {
      hasViewer = true;
    } else {
      hasEditor = true;
      hasViewer = true;
      editorRect.w = layout.content.w / 2.0f;
      viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
    }
    if(hasEditor) {
      const Rect writing = editorWritingRect(editorRect);
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
      const Rect page = ui::pageRectIn(viewerRect);
      const int maxScroll = ui.viewerMaxScroll;
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
    // The search field is part of the sidebar but not part of its row list, so
    // it takes the click before any row arithmetic happens.
    if(contains(searchBoxRect(layout.sidebar), x, y)) {
      if(contains(ui.searchScopeToggle, x, y)) {
        ui.searchScope = ui::nextSearchScope(ui.searchScope);
        ui.state.setSearch(ui.search.text(), ui.searchScope);
        ui.status = "Searching " + std::string(ui::searchScopeName(ui.searchScope));
        return;
      }
      // Clicking a text field puts the caret where you clicked. Before, it only
      // moved focus, and the insertion point stayed pinned to the end.
      const Rect fieldRect = searchTextRect(layout.sidebar, text);
      ui.focus = FocusArea::Search;
      const auto offset = fieldOffsetAtX(text, ui.search, fieldRect, x);
      ui.search.editor.moveCursor(offset);
      ui.selectingFieldText = true;
      ui.fieldSelectionAnchor = offset;
      return;
    }

    ui.focus = FocusArea::Folders;
    const auto index = sidebarRowAt(ui, sidebarListRect(layout.sidebar), x, y);
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
      if(row.kind == SidebarRow::Kind::SearchResult) openNoteMenu(ui, x, y);
      else if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == ui::TreeRowKind::Note) openNoteMenu(ui, x, y);
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
  if(contains(layout.titleBar, x, y)) {
    if(pressWindowButton(ui, x, y, button)) return;
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
      ui.search.reset();
      selectNoteAt(ui, 0);
      return;
    }
    return;
  }
  if(contains(layout.content, x, y)) {
    // The live surface's own chrome sits above the text, so a link underneath it
    // must not swallow the click.
    const bool overLiveChrome = ui.state.workspace().paneMode() == ui::PaneMode::Live &&
                                (ui.livePage.gutterAt(x, y).has_value() || !ui.livePage.toolbarAt(x, y).empty() ||
                                 ui.livePage.foldAt(x, y).has_value() || ui.livePage.copyButtonAt(x, y).has_value());
    if(ui.state.workspace().paneMode() != ui::PaneMode::Editor && !overLiveChrome) {
      for(const auto& link : ui.linkRegions) {
        if(contains(link.rect, x, y)) {
          const auto target = link.target;
          if(link.wiki) {
            openWikiLink(ui, target);
            return;
          }
          const auto hash = target.find('#');
          const auto filePart = hash == std::string::npos ? target : target.substr(0, hash);
          const auto anchorPart = hash == std::string::npos ? std::string() : target.substr(hash + 1);
          if(filePart.empty() && !anchorPart.empty()) {
            if(jumpToAnchor(ui, anchorPart)) {
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
            if(sameNote && jumpToAnchor(ui, anchorPart)) {
              ui.status = "Jumped to " + anchorPart;
              return;
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
    if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
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
        const std::string body {source.substr(block.contentStart(), block.contentEnd() - block.contentStart())};
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
      ui.clearBlockSelection();
      // A task checkbox is a control, not text: ticking it must not move the
      // caret or start a selection.
      if(const auto blockStart = ui.livePage.checkboxAt(x, y)) {
        const std::size_t caret = ui.editor.cursor();
        if(applyEdit(ui, doc::toggleTodo(ui.editor.text(), *blockStart, editorBlocks(ui)))) {
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
    if(ui.state.workspace().paneMode() == ui::PaneMode::Viewer) ui.focus = FocusArea::Viewer;
    else if(ui.state.workspace().paneMode() == ui::PaneMode::Split && x >= layout.content.x + layout.content.w / 2.0f) ui.focus = FocusArea::Viewer;
    else {
      ui.focus = FocusArea::Editor;
      Rect editorRect = layout.content;
      if(ui.state.workspace().paneMode() == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
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
         applyEdit(ui, doc::moveBlocksTo(ui.editor.text(), ui.dragBlockAnchor, ui.dragBlockFocus, *ui.blockDropOffset,
                                           editorBlocks(ui)))) {
        syncBlockSelectionToEdit(ui);
        ui.status = "Moved block";
      }
      ui.draggingBlock = false;
      ui.blockDropOffset.reset();
    }
    if(ui.selectingEditorText) publishEditorPrimarySelection(ui);
    if(ui.selectingFieldText) {
      if(auto* field = focusedField(ui); field && field->editor.hasSelection()) {
        SDL_SetPrimarySelectionText(field->editor.selectedText().c_str());
      }
    }
    ui.resizingSidebar = false;
    ui.selectingEditorText = false;
    ui.selectingFieldText = false;
    ui.scrollDragTarget = ScrollDragTarget::None;
  }
  if(button != SDL_BUTTON_LEFT || (!ui.draggingNote && !ui.draggingFolder)) return;
  const ShellLayout layout = shellLayout(ui, width, height);
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

int run(ApplicationOptions options) {
  // Names the thread whose latency the user feels, so the trace summaries can
  // rank a 30 ms frame above a 200 ms background rescan that blocks nobody.
  perf::markMainThread();
  // Every way out reports, not only the one through the event loop: --screenshot
  // returns after writing its file and --headless returns before a window
  // exists, and a dump wired into the loop alone is silent for both.
  perf::dumpAtExit();
  dumpFrameTraceAtExit();
  const microcore::perf::StartupScope startup("startup");
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
    // Comma-separated: each title after the first opens in a tab of its own, so
    // a capture can show a strip without having to know any note's id.
    std::string_view rest = options.selectTitle;
    bool first = true;
    while(!rest.empty()) {
      const auto comma = rest.find(',');
      const auto title = rest.substr(0, comma);
      for(const auto& note : ui.state.allNotes()) {
        if(note.title.find(title) == std::string::npos) continue;
        ui.state.selectNote(note.id, !first);
        loadSelectedIntoEditor(ui);
        first = false;
        break;
      }
      if(comma == std::string_view::npos) break;
      rest.remove_prefix(comma + 1);
    }
  }
  if(options.paneMode) {
    ui.state.workspace().setPaneMode(*options.paneMode == 0 ? ui::PaneMode::Editor
      : *options.paneMode == 1 ? ui::PaneMode::Viewer
      : *options.paneMode == 2 ? ui::PaneMode::Split
      : ui::PaneMode::Live);
  }
  if(options.showSidebar) ui.state.workspace().sidebarVisible = *options.showSidebar;
  if(options.showRightPanel) ui.state.workspace().rightPanelVisible = *options.showRightPanel;
  if(!options.rightPanelView.empty()) {
    ui.state.workspace().rightPanelView = ui::rightPanelViewFromName(options.rightPanelView);
  }
  if(!attachFromCli(ui, options.attachPath)) return 1;
  if(options.headless) return 0;

  if(!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  if(options.theme) ui::setThemeMode(*options.theme);

  // Borderless at creation rather than SDL_SetWindowBordered afterwards: on
  // Wayland the decoration is a compositor-side object, so asking for one and
  // then retracting it costs a blocking round-trip to the display server.
  SDL_Window* window = SDL_CreateWindow("micronotes", options.windowWidth, options.windowHeight,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                        SDL_WINDOW_BORDERLESS);
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
  ui::configureRenderer(renderer);

  // Held for the lifetime of the window: SDL keeps the pointer and calls back
  // into it on every pointer press near the frame.
  HitTestContext hitTestContext;
  installWindowHitTest(window, renderer, ui, hitTestContext);

  // Present in step with the display. Without this the renderer tears on one
  // frame and stalls on the next, which reads as jitter even when every frame
  // is well inside budget.
  SDL_SetRenderVSync(renderer, 1);

  // Every layout number in this file is in logical (density-independent) units,
  // and SDL_GetWindowSize reports the same. Scaling the renderer by the window's
  // pixel density is therefore all that is needed to make HIGH_PIXEL_DENSITY
  // produce a sharper image rather than a bigger one. Expressed as a density
  // rather than a ratio against a fixed window size, it stays correct across
  // resizes; the display-changed event below re-reads it when the window moves
  // to a monitor with a different scale.
  const auto applyPixelDensity = [renderer, window]() {
    const float density = SDL_GetWindowPixelDensity(window);
    SDL_SetRenderScale(renderer, density, density);
  };
  applyPixelDensity();
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

  // Everything the loop has to be awake for. Autosave used to be the only one,
  // so it was also the only thing the wait knew about.
  auto deadlines = [&]() -> FrameDeadlines {
    FrameDeadlines out;
    out.autosaveMs = autosaveWaitMs();
    // A drag past the edge of a list has to keep scrolling while the pointer is
    // perfectly still, which produces no events at all.
    out.hint = ui.selectingEditorText || ui.draggingNote || ui.draggingFolder || ui.draggingBlock
                 ? IdleHint::Busy
                 : IdleHint::Idle;
    return out;
  };

  if(!options.searchQuery.empty()) {
    // Not selectAll: a capture wants the caret after the query, the way it sits
    // once the query has been typed.
    ui.search.beginWith(options.searchQuery, false);
    ui.state.setSearch(options.searchQuery, ui.searchScope);
    ui.focus = FocusArea::Search;
  }

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
    else if(which == "wiki-menu") openWikiMenu(ui, ui.editor.cursor());
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
    const WaitDecision wait = chooseWait(deadlines());
    const bool hasEvent = wait.mode == WaitMode::Block
      ? SDL_WaitEvent(&event)
      : SDL_WaitEventTimeout(&event, wait.timeoutMs);
    perf::addCounter(perf::CounterId::FrameEventWakes);
    if(hasEvent) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      int drained = 0;
      do {
        ++drained;
        needsDraw = true;
      if(event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if(event.type == SDL_EVENT_TEXT_INPUT) {
        perf::addCounter(perf::CounterId::InputTextEvents);
        handleText(ui, event.text.text);
      } else if(event.type == SDL_EVENT_KEY_DOWN) {
        perf::addCounter(perf::CounterId::InputKeyEvents);
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
          if(ui.state.workspace().paneMode() == ui::PaneMode::Live) {
            ui.editor.selectRange(ui.editorSelectionAnchor, ui.livePage.offsetAt(event.motion.x, event.motion.y));
            ui.revealEditorCursor = true;
          } else {
            const ShellLayout layout = shellLayout(ui, width, height);
            Rect editorRect = layout.content;
            if(ui.state.workspace().paneMode() == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
            const auto cursor = editorIndexAtPoint(text, ui, editorRect, event.motion.x, event.motion.y);
            ui.editor.selectRange(ui.editorSelectionAnchor, cursor);
            ui.revealEditorCursor = true;
          }
        } else if(ui.scrollDragTarget != ScrollDragTarget::None) {
          const ShellLayout layout = shellLayout(ui, width, height);
          if(ui.scrollDragTarget == ScrollDragTarget::Live) {
            ui.livePage.setScroll(scrollFromThumbY(ui.livePage.pageRect(), event.motion.y, ui.scrollDragOffsetY, ui.livePage.maxScroll()));
          } else if(ui.scrollDragTarget == ScrollDragTarget::Editor) {
            Rect editorRect = layout.content;
            if(ui.state.workspace().paneMode() == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
            const Rect writing = editorWritingRect(editorRect);
            const int maxScroll = editorMaxScroll(text, ui, editorRect);
            ui.editorScroll = scrollFromThumbY(writing, event.motion.y, ui.scrollDragOffsetY, maxScroll);
            ui.revealEditorCursor = false;
          } else if(ui.scrollDragTarget == ScrollDragTarget::Viewer) {
            Rect viewerRect = layout.content;
            if(ui.state.workspace().paneMode() == ui::PaneMode::Split) {
              const float split = layout.content.w / 2.0f;
              viewerRect = {layout.content.x + split, layout.content.y, layout.content.w - split, layout.content.h};
            }
            const Rect page = ui::pageRectIn(viewerRect);
            ui.viewerScroll = scrollFromThumbY(page, event.motion.y, ui.scrollDragOffsetY, ui.viewerMaxScroll);
          }
        } else if(ui.draggingNote || ui.draggingFolder) {
          const ShellLayout layout = shellLayout(ui, width, height);
          const auto row = sidebarRowAt(ui, sidebarListRect(layout.sidebar), event.motion.x, event.motion.y);
          ui.sidebarDropRow = row && ui.sidebarRows[*row].kind == SidebarRow::Kind::Tree
                                ? row
                                : std::optional<std::size_t> {};
        } else if(ui.resizingSidebar) {
          ui.state.workspace().sidebarWidth = std::clamp(static_cast<float>(event.motion.x), ui::kMinSidebarWidth,
                                                         std::max(ui::kMinSidebarWidth, static_cast<float>(width) - 520.0f));
        }
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL) {
        routeWheel(text, ui, event.wheel.y, width, height);
      } else if(event.type == SDL_EVENT_DROP_FILE) {
        if(event.drop.data) attachPathToEditor(ui, event.drop.data);
      } else if(event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        applyDisplayScale();
      } else if(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        // Only when there is nothing unsaved: reloading under a dirty buffer
        // would put the note back to what is on disk.
        if(ui.state.hasLibrary() && !ui.editor.dirty()) {
          invalidateWikiNotes(ui);
          ui.state.refreshLibrary();
        }
      }
        // A held key or a fast trackpad refills the queue as fast as it
        // empties, and draining it whole starves the paint: the window stops
        // updating while input is still arriving. Stop at the budget and let
        // the frame go out; the rest is still queued.
        if(shouldYieldEventDrain(drained, needsDraw)) break;
      } while(SDL_PollEvent(&event));
    }
    if(applyPendingWindowAction(window, ui, running)) needsDraw = true;

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
    } else {
      perf::addCounter(perf::CounterId::FrameRepaintsSkipped);
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
