#pragma once

#include "app/Shell.h"

// What Escape means, as one function over the shell's state.
//
// Escape is the key with the most claims on it: a running query, a
// find-in-note, a half-typed notebook name, a tag filter, a block selection,
// and -- when none of those are in force -- stepping out of the text in the
// page. Every one of those was a separate `if` in `Application.cpp`'s
// key handler, all of them fired on the same press, and one of them (the tag
// filter) was simply missing, which is how a tag filter came to be a one-way
// door.
//
// Written down here as a stack instead: **one press undoes one narrowing**,
// outermost claim last, and the answer says which one it was. That is the rule
// the pile of `if`s was an approximation of, it is the rule a reader expects
// from every other application, and unlike a pile of `if`s it can be tested
// without a window.
namespace micronotes::app {

// What a press of Escape undid, in the order the press considers them.
enum class Dismissed {
  // Nothing was narrowing the view. The caller decides what Escape means in
  // whatever has focus.
  Nothing,
  Search,     // a running query, which owns the whole row list
  Find,       // find-in-note
  FolderName, // a notebook being named
  TagFilter   // the library filtered to one tag
};

// Undoes the innermost narrowing in force and reports which it was.
//
// The order is the order things were put *on*, so leaving reverses arriving: a
// query typed while a tag filter was running comes off first, because it is
// what the reader is looking at. Nothing else is touched -- in particular the
// open note stays open, since none of these are questions about what is being
// read.
Dismissed dismissOne(UiRuntime& ui);

}
