#include "app/BlockMenus.h"

#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Shell.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "ui/Actions.h"
#include "ui/Overlay.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace micronotes::app {
namespace {

// The block types, as menu rows. One table feeds the slash menu and the
// turn-into menu, so a new block type appears in both at once.
std::vector<ui::OverlayItem> blockKindItems() {
  std::vector<ui::OverlayItem> items;
  for(const auto& entry : blockKinds()) {
    items.push_back(ui::menuItem(std::string(entry.id), std::string(entry.label))
                      .withDetail(std::string(entry.detail)));
  }
  return items;
}

}

namespace {

ui::Overlay turnIntoOverlay() {
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "turn-into";
  overlay.title = "Turn into";
  overlay.width = 260.0f;
  overlay.filterable = true;
  overlay.placeholder = "Filter block types";
  overlay.items = blockKindItems();
  return overlay;
}

}

void openTurnIntoMenu(UiRuntime& ui, float x, float y) {
  ui::Overlay overlay = turnIntoOverlay();
  overlay.anchored = true;
  overlay.anchorX = x;
  overlay.anchorY = y;
  ui.overlays.open(std::move(overlay));
}

void openTurnIntoMenu(UiRuntime& ui) {
  ui.overlays.open(turnIntoOverlay());
}

// `slashStart` is the "/" the user typed; committing erases [slashStart, caret)
// before the block transform runs.
void openSlashMenu(UiRuntime& ui, std::size_t slashStart) {
  ui.slash.start = slashStart;
  ui::Overlay overlay;
  overlay.kind = ui::OverlayKind::List;
  overlay.id = "slash-menu";
  overlay.title = "Insert block";
  overlay.width = 320.0f;
  overlay.filterable = true;
  overlay.placeholder = "Filter block types";
  overlay.hint = "Enter inserts, Esc keeps typing";
  // The note behind stays lit: this list is about the line the caret is on.
  overlay.dimsBehind = false;
  overlay.items = blockKindItems();
  ui.overlays.open(std::move(overlay));
}

void commitSlashMenu(UiRuntime& ui, const std::string& itemId) {
  const std::size_t caret = ui.editor.cursor();
  const std::size_t start = std::min(ui.slash.start, caret);
  if(start < caret) {
    ui.editor.replaceRange(start, caret, "");
    ui.markEdited();
  }
  performBlockCommand(ui, itemId);
}

// Appends the selected blocks to another note and removes them from this one.
// The source edit goes through the undo stack as any block edit does; the
// target is not open, so it is written directly.
void moveBlocksToNote(UiRuntime& ui, const std::string& targetId) {
  const auto target = ui.state.catalog().findNote(targetId);
  if(!target || target->id == ui.state.selection().noteId) {
    ui.status = "Pick a different note";
    return;
  }
  const auto [from, to] = blockCommandRange(ui);
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
    ui.status = "Moved blocks to " + target->title;
  }
}

}
