#pragma once

#include "CoreAliases.h"

#include <string>
#include <string_view>
#include <vector>

namespace micronotes::library {

struct NoteMetadata {
  // How `tags:` was written in the file. micronotes rewrites the whole header
  // on save, so remembering the form is what stops a note that arrived in YAML
  // block form from being silently reflowed into micronotes' inline form.
  enum class TagForm {
    Inline,  // tags: alpha beta
    Flow,    // tags: [alpha, beta]
    Block    // tags:\n  - alpha
  };

  std::string id;
  std::string title;
  // A single emoji shown beside the note in the tree and the breadcrumbs.
  // Empty means the note has no icon, not that it has a default one.
  std::string icon;
  std::vector<std::string> tags;
  TagForm tagForm = TagForm::Inline;
  // Whether the note's body opened with a `# <title>` heading that said nothing
  // its own name did not.
  //
  // micronotes draws the name above the body, so a note carrying it as a
  // heading as well printed it twice - once as the page's title and once as its
  // first line. The heading is split off with the front matter on load and
  // written back on save, which is what keeps it in the file for every other
  // tool that expects it there while the body the user edits holds the name
  // exactly once. False for anything micronotes created: a new note's Markdown
  // never grows a line that only repeats its own file name.
  bool titleHeading = false;
  // Front matter keys micronotes does not model, kept verbatim and in order.
  // The header is rewritten in full on every save, so without this any key
  // another tool wrote - an alias list, a CSS class, a publish date - would be
  // destroyed by the first autosave after the note was opened here.
  std::vector<std::string> extra;
};

std::string generateNoteId();

// Identity for a note file whose front matter carries no `id`. Derived from the
// library-relative path so it stays stable across runs, and hashed so it is
// safe to use as a SQLite key and as a recovery-file name. Notes created or
// saved by micronotes get a generated id instead.
std::string fallbackNoteId(std::string_view relativePath);
std::string metadataHeader(const NoteMetadata& metadata);
NoteMetadata parseMetadata(std::string_view markdown);
std::string stripMetadataHeader(std::string_view markdown);

// How many bytes at the front of `body` are a `# <title>` heading repeating the
// note's own name, plus the blank line under it. Zero when the body does not
// open with one.
//
// Deliberately literal about what it will claim: the heading has to be an ATX
// `# ` at the first level, and its text has to be `title` exactly once the
// surrounding spaces are off. A heading that says anything else says something
// the note's name does not, and stays in the body where the reader put it - so
// a level-2 heading, a closed `# Title #`, a setext underline and a title that
// differs by a word or by case are all left alone. Anything this does claim is
// re-emitted by metadataHeader(), so what it takes out of the body goes back
// into the file byte for byte.
std::size_t titleHeadingLength(std::string_view body, std::string_view title);

}
