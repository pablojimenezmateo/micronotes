#include "core/editor/TextField.h"

namespace microcore::editor {

void TextField::beginWith(std::string value, bool selectAll) {
  editor.setText(std::move(value));
  scrollX = 0.0f;
  if(selectAll) editor.selectAll();
}

void TextField::reset() {
  editor.setText(std::string());
  scrollX = 0.0f;
}

FieldKeyResult applyKeyToField(TextField& field, SDL_Keycode key, bool ctrl, bool shift) {
  auto& value = field.editor;
  switch(key) {
    case SDLK_LEFT:
      if(ctrl) value.moveWordLeft(shift);
      else value.moveLeft(shift);
      return FieldKeyResult::Moved;
    case SDLK_RIGHT:
      if(ctrl) value.moveWordRight(shift);
      else value.moveRight(shift);
      return FieldKeyResult::Moved;
    case SDLK_HOME:
      value.moveHome(shift);
      return FieldKeyResult::Moved;
    case SDLK_END:
      value.moveEnd(shift);
      return FieldKeyResult::Moved;
    case SDLK_BACKSPACE:
      if(value.hasSelection()) value.eraseSelection();
      else if(ctrl) value.eraseWordBefore();
      else value.erasePrevious();
      return FieldKeyResult::Changed;
    case SDLK_DELETE:
      if(value.hasSelection()) value.eraseSelection();
      else if(ctrl) value.eraseWordAfter();
      else value.eraseNext();
      return FieldKeyResult::Changed;
    case SDLK_A:
      if(!ctrl) return FieldKeyResult::Ignored;
      value.selectAll();
      return FieldKeyResult::Moved;
    case SDLK_Z:
      if(!ctrl) return FieldKeyResult::Ignored;
      return value.undo() ? FieldKeyResult::Changed : FieldKeyResult::Ignored;
    case SDLK_Y:
      if(!ctrl) return FieldKeyResult::Ignored;
      return value.redo() ? FieldKeyResult::Changed : FieldKeyResult::Ignored;
    default:
      return FieldKeyResult::Ignored;
  }
}

}
