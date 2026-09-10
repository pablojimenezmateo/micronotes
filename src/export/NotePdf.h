#pragma once

#include <cstddef>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Exporting notes as a PDF.
//
// This layer is above `ui` and below `app`: it needs the theme's palette and
// the vendored faces, and it must not need a window, a renderer or a shell.
// That is what lets an export be tested without opening one -- the output is
// bytes, and the bytes are a function of the notes and nothing else.
namespace micronotes::exporting {

// One note, as the exporter needs it: its name and its Markdown, with the
// front matter already taken off. The exporter never touches the library and
// never reads a file of its own except the pictures a note points at, so
// whoever calls it has already decided what a note *is*.
struct PdfNote {
  std::string title;
  std::string body;
  std::vector<std::string> tags;
};

struct PdfRequest {
  // In the order they will appear. Notes never share a page.
  std::vector<PdfNote> notes;
  // What the file calls itself: a note's title for one note, a notebook's name
  // for a folder.
  std::string documentTitle;
  // Where `![](...)` targets are resolved from. Empty exports no pictures.
  std::filesystem::path libraryRoot;
  // Whether a `[[target]]` names a note that exists, which decides its colour
  // exactly as it does on screen. Unset means "assume it does".
  std::function<bool(std::string_view)> wikiLinkResolves;
  // A PDF date string, or empty to write no date at all.
  //
  // Passed in rather than read from the clock here, so that exporting the same
  // note twice produces the same bytes. That is what makes a test able to
  // compare two exports, and it is the same argument the screenshot tests make.
  std::string creationDate;
};

struct PdfResult {
  bool ok = false;
  std::size_t pages = 0;
  // Empty when `ok`. Something a status line can show when not.
  std::string error;
};

// Composes the document. `bytes` receives the whole file.
PdfResult renderPdf(const PdfRequest& request, std::string* bytes);

// The same, written to `file` -- through the durable write every other file in
// this application goes through, so a full disk leaves the previous PDF intact
// rather than a truncated one in its place.
PdfResult writePdf(const PdfRequest& request, const std::filesystem::path& file);

// `D:YYYYMMDDHHmmSS` in local time, which is the form a PDF date takes.
std::string pdfDate(std::time_t when);

}
