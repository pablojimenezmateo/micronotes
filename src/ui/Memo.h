#pragma once

#include <utility>

// A value kept only as long as the inputs it was derived from have not moved.
//
// Five panels in the shell have one of these, and each was three or four loose
// fields on `UiRuntime`: the value, a `valid` flag, and the key spelled out one
// field at a time, with the comparison written at the site that reads it and
// the assignment written at the site that fills it.
//
// That shape has one specific failure, and it is the reason this exists. The
// two lists drift. A key gains a field, the comparison learns about it and the
// assignment does not -- or the other way round -- and the memo then answers
// for inputs it was not built from. Nothing catches it, because the memo still
// *works*: it is wrong only when the field nobody stored is the field that
// changed, which is the case nobody tests.
//
// Here there is one list. `get` compares the whole key and `store` replaces the
// whole key, so a field cannot be in one and not the other.
//
// Not a cache: there is one entry, because every one of these answers a
// question about *the thing the reader is looking at*, and the previous note's
// outline has no readers. A keyed cache with eviction is `ComplexRenderCache`
// in `app/NoteCaches.h`, and it is a different shape for that reason.
namespace micronotes::ui {

template <typename Value, typename Key>
class Memo {
public:
  // The standing value, when it was built from exactly this key. Null when it
  // was built from something else, or from nothing yet.
  const Value* get(const Key& key) const {
    return valid_ && key_ == key ? &value_ : nullptr;
  }
  Value* get(const Key& key) {
    return valid_ && key_ == key ? &value_ : nullptr;
  }

  // Replaces the value and the key it belongs to, together. Hands the stored
  // value back so a caller can return it as the answer it was asked for.
  const Value& store(Key key, Value value) {
    key_ = std::move(key);
    value_ = std::move(value);
    valid_ = true;
    return value_;
  }

  // The same, for a producer that fills the value in place rather than building
  // one to move in. `ui::outlineInto` and the sidebar's row build both clear and
  // refill a vector so the last one's allocation is reused, and a `store` that
  // takes it by value would throw that away -- on the outline, once per
  // keystroke.
  //
  // The key is taken first, so an exception out of the producer leaves a memo
  // that is merely wrong-and-stale rather than one claiming a key whose value
  // was never filled: `get` then answers null for the key it holds, which is
  // the safe direction.
  Value& rebuild(Key key) {
    key_ = std::move(key);
    valid_ = true;
    return value_;
  }

  // Forgets the value, for a caller that knows its inputs have moved but not
  // what they have moved to. Prefer a key that says so: a key cannot be
  // forgotten, only unequal.
  void invalidate() { valid_ = false; }

  // What is held, whatever it was built from. For the reader that has just
  // called `store` or `get` and only wants the value again -- a draw that
  // measured on one line and paints on the next. A reader that has not asked
  // whether it is current should be asking `get`.
  const Value& value() const { return value_; }
  bool valid() const { return valid_; }

private:
  bool valid_ = false;
  Key key_ {};
  Value value_ {};
};

}
