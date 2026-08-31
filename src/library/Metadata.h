#pragma once

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

}
