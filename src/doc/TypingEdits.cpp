#include "doc/TypingEdits.h"

#include "doc/EditContext.h"

#include <string>

namespace micronotes::doc {

Edit continueList(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  const bool list = isListKind(block.kind);
  const bool quote = block.kind == BlockKind::Quote || block.kind == BlockKind::Callout;
  if(!list && !quote) return edit;
  if(caret < block.contentStart()) return edit;

  if(block.contentEnd() <= block.contentStart()) {
    // Enter on an empty item leaves the list instead of adding another.
    if(list && block.listDepth > 0) return outdent(source, caret, blocks);
    // Dropping the marker alone is not enough to get out: "- one\ntext" and
    // "> a\ntext" are lazy continuations, so what the file says would still be
    // one list item or one quote, whatever the screen showed. The blank line is
    // what actually ends the block.
    edit.valid = true;
    edit.start = block.start;
    edit.end = block.contentStart();
    edit.text = "\n";
    edit.cursor = block.start + 1;
    return edit;
  }

  edit.valid = true;
  edit.start = caret;
  edit.end = caret;
  edit.text = "\n" + (list ? blockMarker(block.kind, 0, block.listDepth, block.ordinal + 1, false,
                                        block.listMarker)
                           : std::string("> "));
  edit.cursor = caret + edit.text.size();
  return edit;
}

Edit closeFence(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind != BlockKind::Code) return edit;
  if(block.contentEnd() != block.end()) return edit;  // a closing fence is already there
  if(caret > lineEndFrom(source, block.start)) return edit;

  std::size_t i = block.start;
  while(i < source.size() && (source[i] == ' ' || source[i] == '\t')) ++i;
  const char marker = i < source.size() ? source[i] : '`';
  if(marker != '`' && marker != '~') return edit;
  std::size_t length = 0;
  while(i + length < source.size() && source[i + length] == marker) ++length;

  edit.valid = true;
  edit.start = caret;
  edit.end = caret;
  edit.text = "\n\n" + std::string(length, marker);
  edit.cursor = caret + 1;
  return edit;
}

Edit outdentOrUnwrap(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(caret != block.contentStart() || block.contentStart() <= block.start) return edit;
  switch(block.kind) {
    case BlockKind::Complex:
    case BlockKind::Code:
    case BlockKind::Divider:
    case BlockKind::Blank:
      return edit;
    default:
      break;
  }
  if(isListKind(block.kind) && block.listDepth > 0) return outdent(source, caret, blocks);
  edit.valid = true;
  edit.start = block.start;
  edit.end = block.contentStart();
  edit.cursor = block.start;
  return edit;
}

Edit applyMarkdownShortcut(std::string_view source, std::size_t caret, BlockSpan blocks) {
  Edit edit;
  // This runs on every typed space, so reject on the two bytes every shortcut
  // ends with before paying for a scan of the buffer.
  if(caret < 3 || caret > source.size() || source[caret - 1] != ' ' || source[caret - 2] != ']') return edit;
  const Context context = contextAt(source, caret, blocks);
  const SourceBlock& block = context.blocks[context.index];
  if(block.kind == BlockKind::Code || block.kind == BlockKind::Complex) return edit;
  const std::size_t from = block.contentStart();
  if(caret <= from || caret > source.size()) return edit;

  const std::string_view typed = source.substr(from, caret - from);
  if(typed != "[] " && typed != "[ ] " && typed != "[x] " && typed != "[X] ") return edit;
  const bool checked = typed == "[x] " || typed == "[X] ";

  // A bullet only needs the checkbox normalised; a paragraph needs the whole
  // task marker. Every other Markdown shortcut is already what the user typed.
  std::string replacement;
  if(block.kind == BlockKind::Bullet) replacement = checked ? "[x] " : "[ ] ";
  else if(block.kind == BlockKind::Paragraph) replacement = blockMarker(BlockKind::Todo, 0, 0, 0, checked);
  else return edit;

  edit.valid = true;
  edit.start = from;
  edit.end = caret;
  edit.text = replacement;
  edit.cursor = from + replacement.size();
  return edit;
}

}
