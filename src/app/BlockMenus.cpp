#include "app/BlockMenus.h"

#include "app/EditCommands.h"
#include "app/EditorBlocks.h"
#include "app/Shell.h"
#include "doc/BlockScan.h"
#include "doc/Edits.h"
#include "doc/Fold.h"
#include "ui/Actions.h"
#include "ui/Overlay.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace micronotes::app {
namespace {

// The block types, as menu rows. One table feeds the slash menu, the turn-into
// menu and the block menu, so a new block type appears in all three at once.
std::vector<ui::OverlayItem> blockKindItems() {
  std::vector<ui::OverlayItem> items;
  for(const auto& entry : blockKinds()) items.push_back({entry.id, entry.label, entry.detail, "", true, false});
  return items;
}

}


void openTurnIntoMenu(UiRuntime& ui, float x, float y) {
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

void openBlockMenu(UiRuntime& ui, float x, float y) {
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
void openSlashMenu(UiRuntime& ui, std::size_t slashStart) {
  ui.slash.start = slashStart;
  ui.slash.inserts = false;
  ui.blockSelection.clear();
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

// Opened from the gutter's insert button: nothing is written until a block type
// is chosen, so dismissing the menu leaves the note exactly as it was.
void openInsertMenu(UiRuntime& ui, std::size_t blockStart) {
  openSlashMenu(ui, ui.editor.cursor());
  ui.slash.inserts = true;
  ui.slash.afterBlock = blockStart;
}

void commitSlashMenu(UiRuntime& ui, const std::string& itemId) {
  const BlockKindEntry* entry = blockKindFor(itemId);
  if(ui.slash.inserts) {
    if(entry && applyEdit(ui, doc::insertBlockAfter(ui.editor.text(), ui.slash.afterBlock, entry->kind, entry->level,
                                             editorBlocks(ui)))) {
      ui.status = entry->label;
    }
    return;
  }
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
    ui.blockSelection.clear();
    ui.status = "Moved blocks to " + target->title;
  }
}

}
