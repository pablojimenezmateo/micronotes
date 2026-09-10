#include "ShellFixture.h"
#include "TempDir.h"
#include "TestSupport.h"

#include "app/ExportPdf.h"
#include "app/ExportState.h"
#include "app/Notes.h"
#include "export/NotePdf.h"

#include <string>

namespace {

using micronotes::exporting::PdfNote;
using micronotes::exporting::PdfRequest;
using micronotes::exporting::PdfResult;

PdfRequest requestOf(std::string title, std::string body) {
  PdfRequest request;
  PdfNote note;
  note.title = std::move(title);
  note.body = std::move(body);
  request.notes.push_back(std::move(note));
  request.documentTitle = request.notes.front().title;
  // Fixed, so the same notes give the same bytes. See `PdfRequest`.
  request.creationDate = "D:20260910120000";
  return request;
}

std::string exported(const PdfRequest& request) {
  std::string bytes;
  const PdfResult result = micronotes::exporting::renderPdf(request, &bytes);
  micronotes::tests::require(result.ok, "the export failed: " + result.error);
  return bytes;
}

// How many pages the file says it has. Read off the page tree, which is the
// only uncompressed place the count appears.
int pageCount(const PdfRequest& request) {
  std::string bytes;
  const PdfResult result = micronotes::exporting::renderPdf(request, &bytes);
  micronotes::tests::require(result.ok, "the export failed: " + result.error);
  return static_cast<int>(result.pages);
}

std::string longNote(int paragraphs) {
  std::string body;
  for(int i = 0; i < paragraphs; ++i) {
    body += "Paragraph " + std::to_string(i) +
            ", with enough words in it to take up most of a line of the page and then wrap "
            "onto a second one, which is what makes a note this long actually long.\n\n";
  }
  return body;
}

// How many `/Link` annotations the file carries. The page objects and the
// annotations themselves are uncompressed dictionaries, so this is readable
// straight out of the bytes -- unlike the text, which is inside a stream.
int linkCount(const std::string& bytes) {
  int count = 0;
  for(std::size_t at = bytes.find("/Subtype /Link"); at != std::string::npos;
      at = bytes.find("/Subtype /Link", at + 1)) {
    ++count;
  }
  return count;
}

}

// The structural claims a reader checks before it draws anything. A file that
// fails any of them opens as an error dialog rather than as a document.
MICRONOTES_TEST(pdf_export_writes_a_document_a_reader_can_open) {
  const std::string file = exported(requestOf("A note", "# Heading\n\nSome text.\n"));
  MICRONOTES_REQUIRE(file.rfind("%PDF-1.7", 0) == 0);
  MICRONOTES_REQUIRE(file.find("/Type /Catalog") != std::string::npos);
  MICRONOTES_REQUIRE(file.find("/Type /Pages") != std::string::npos);
  MICRONOTES_REQUIRE(file.find("/Type /Page ") != std::string::npos);
  MICRONOTES_REQUIRE(file.find("/MediaBox [ 0 0 595.28 841.89 ]") != std::string::npos);
  MICRONOTES_REQUIRE(file.find("startxref") != std::string::npos);
  MICRONOTES_REQUIRE(file.find("%%EOF") != std::string::npos);
}

// Two exports of the same notes are the same bytes. It is what lets one export
// be diffed against another, which is the only practical way to see what a
// change to the composer actually did -- the same argument the screenshot
// comparisons in `docs/performance.md` make.
MICRONOTES_TEST(pdf_export_is_byte_identical_for_the_same_notes) {
  const auto request = requestOf("A note", "# Heading\n\nSome text with `code` in it.\n");
  MICRONOTES_REQUIRE(exported(request) == exported(request));
}

// Text has to come back out. With `Identity-H` the bytes in the file are glyph
// numbers rather than characters, so without a `ToUnicode` map every copy out
// of the PDF -- and every search inside it -- yields nonsense.
MICRONOTES_TEST(pdf_export_maps_its_glyphs_back_to_characters) {
  const std::string file = exported(requestOf("A note", "Some ordinary words.\n"));
  MICRONOTES_REQUIRE(file.find("/ToUnicode") != std::string::npos);
  MICRONOTES_REQUIRE(file.find("/Encoding /Identity-H") != std::string::npos);
}

// A face nothing is set in is not embedded. Inter and JetBrains Mono are about
// 600 KB and 275 KB apiece, so carrying all seven into every export would add
// three megabytes to a one-paragraph note.
MICRONOTES_TEST(pdf_export_embeds_only_the_faces_it_used) {
  const std::string prose = exported(requestOf("Prose", "Just some words, no code.\n"));
  MICRONOTES_REQUIRE(prose.find("/Inter-Regular") != std::string::npos);
  MICRONOTES_REQUIRE(prose.find("/JetBrainsMono") == std::string::npos);
  MICRONOTES_REQUIRE(prose.find("/Inter-SemiBoldItalic") == std::string::npos);

  const std::string code = exported(requestOf("Code", "Some `inline code` here.\n"));
  MICRONOTES_REQUIRE(code.find("/JetBrainsMono-Regular") != std::string::npos);
}

// A note longer than a page becomes more than one page, and a note shorter
// than one does not.
// A note carries the glyphs it showed, not the faces it was set in.
//
// This is what closing TD-37 means in file sizes, which is the cost it named.
// A one-paragraph note used to export at 447 KB, of which 445 KB was the whole
// of `Inter-Regular.otf` -- 2,900 glyphs and 26 KB of character map, for a
// note that uses forty letters. The bounds below are deliberately loose: they
// are there to fail if a face ever goes in whole again, not to pin a number
// that moves whenever a glyph's outline changes.
MICRONOTES_TEST(pdf_export_embeds_only_the_glyphs_it_showed) {
  const std::string prose =
    exported(requestOf("Prose", "One paragraph of ordinary words and nothing else at all.\n"));
  // The whole sans face alone is 610 KB, and about 420 KB of that survives
  // compression.
  MICRONOTES_REQUIRE(prose.size() < 200u * 1024u);

  // Four faces is four font programs, and a note that uses all of them must
  // still come out under what one whole face used to cost.
  const std::string every = exported(requestOf(
    "Every face", "# Heading\n\nWith *italic*, **bold** and `code` in one paragraph.\n"));
  MICRONOTES_REQUIRE(every.find("/Inter-Regular") != std::string::npos);
  MICRONOTES_REQUIRE(every.find("/Inter-Italic") != std::string::npos);
  MICRONOTES_REQUIRE(every.find("/JetBrainsMono-Regular") != std::string::npos);
  MICRONOTES_REQUIRE(every.size() < 400u * 1024u);

  // And the pages still say what they said: a subset that dropped a glyph the
  // note shows is a blank box, which no size assertion would notice.
  MICRONOTES_REQUIRE(pageCount(requestOf("Prose", "One paragraph.\n")) == 1);
}

// The glyphs a note shows are the glyphs the *font program* carries, so two
// notes with different text carry different amounts of it. A subsetter that
// quietly embedded everything would make these the same size.
MICRONOTES_TEST(pdf_export_carries_less_for_a_shorter_note) {
  const std::string few = exported(requestOf("Few", "abc\n"));
  std::string many = "Every letter: ";
  for(char c = 'a'; c <= 'z'; ++c) many += c;
  for(char c = 'A'; c <= 'Z'; ++c) many += c;
  const std::string all = exported(requestOf("Many", many + "\n"));
  MICRONOTES_REQUIRE(few.size() < all.size());
}


MICRONOTES_TEST(pdf_export_paginates_a_note_longer_than_a_page) {
  MICRONOTES_REQUIRE(pageCount(requestOf("Short", "One line.\n")) == 1);
  const int pages = pageCount(requestOf("Long", longNote(60)));
  MICRONOTES_REQUIRE(pages > 3);
}

// A table taller than a page breaks between its rows. Before it did, a long
// table was one page of table and a page and a half of nothing -- the block
// was atomic, so everything past the page edge was simply drawn off it.
MICRONOTES_TEST(pdf_export_breaks_a_long_table_between_rows) {
  std::string body = "| Left | Right |\n| --- | --- |\n";
  for(int i = 0; i < 70; ++i) {
    body += "| row " + std::to_string(i) + " | value " + std::to_string(i) + " |\n";
  }
  MICRONOTES_REQUIRE(pageCount(requestOf("A table", body)) > 1);
}

// A cell's own runs reach the file, which is what closing TD-39 means in
// practice: the faces a cell's markup asks for are embedded, and they can only
// have been asked for by the cell -- nothing else in this note is set in them.
//
// It reads the faces rather than the text because the text is inside a
// compressed content stream. A cell set as `markdown::plainText` needed the
// regular and the strong face and nothing else, which is exactly what this
// would have caught.
MICRONOTES_TEST(pdf_export_sets_a_table_cell_in_the_faces_its_markup_asks_for) {
  const std::string plain =
    exported(requestOf("Plain", "| Head |\n| --- |\n| ordinary words |\n"));
  MICRONOTES_REQUIRE(plain.find("/Inter-Italic") == std::string::npos);
  MICRONOTES_REQUIRE(plain.find("/JetBrainsMono") == std::string::npos);

  const std::string marked =
    exported(requestOf("Marked", "| Head |\n| --- |\n| *italic* and `code` |\n"));
  MICRONOTES_REQUIRE(marked.find("/Inter-Italic") != std::string::npos);
  MICRONOTES_REQUIRE(marked.find("/JetBrainsMono-Regular") != std::string::npos);
}

// One note, one answer about whether a `[[target]]` resolves. A wikilink in a
// table used to export as a resolved link while the same wikilink in the
// paragraph above it exported as pending, because only the note's own blocks
// were asked and the md4c path was not.
MICRONOTES_TEST(pdf_export_asks_about_a_wikilink_in_a_table_too) {
  // Nothing outside the table, so only the md4c path can be what differs.
  const std::string body = "| Head |\n| --- |\n| [[Missing]] |\n";
  const auto shapeWith = [&body](bool exists) {
    PdfRequest request = requestOf("Wikilinks", body);
    request.wikiLinkResolves = [exists](std::string_view) { return exists; };
    return exported(request);
  };
  // The pending colour and the accent are different ink, so the two exports
  // differ -- and they differ whichever of the two paths drew the link.
  MICRONOTES_REQUIRE(shapeWith(true) != shapeWith(false));
}

// A table that runs over a page break repeats the row its columns are named in
// (TD-40), and the row it repeats is page space the rows themselves do not
// account for. The composer reserves it off the same function the painter asks,
// and this is the guard that the two staying in step still paginates: an
// upper bound, because a reservation the rows do not shrink for would fit one
// fewer row per page each time round and end at a page per row.
//
// That the header is charged only past the header row, and that the rows still
// tile the block, are `tests/RenderLayoutTests.cpp` -- which is where the
// arithmetic is rather than where its output is compressed.
MICRONOTES_TEST(pdf_export_repeats_a_continued_tables_header_row) {
  std::string body = "| Column A | Column B |\n| --- | --- |\n";
  for(int i = 0; i < 70; ++i) {
    body += "| row " + std::to_string(i) + " | value " + std::to_string(i) + " |\n";
  }
  const int pages = pageCount(requestOf("A long table", body));
  MICRONOTES_REQUIRE(pages > 1);
  MICRONOTES_REQUIRE(pages < 6);
}

// Notes never share a page: a notebook's PDF is its notes bound together, not
// its notes run into each other.
MICRONOTES_TEST(pdf_export_gives_every_note_a_page_of_its_own) {
  PdfRequest request;
  for(int i = 0; i < 4; ++i) {
    PdfNote note;
    note.title = "Note " + std::to_string(i);
    note.body = "One short line.\n";
    request.notes.push_back(note);
  }
  request.documentTitle = "A notebook";
  request.creationDate = "D:20260910120000";
  MICRONOTES_REQUIRE(pageCount(request) == 4);
}

// Every construct the reading pane draws goes through the exporter's own
// painter, and each of them has a way of reaching a null face or an empty line
// list. This is the smoke test that says the whole vocabulary composes.
MICRONOTES_TEST(pdf_export_handles_every_kind_of_block) {
  const std::string body =
    "# One\n\n## Two\n\nA paragraph with **bold**, *italic*, ~~struck~~ and `code`.\n\n"
    "- a bullet\n- another\n  - nested\n\n1. first\n2. second\n\n"
    "- [ ] open\n- [x] done\n\n> a quote\n> continued\n\n"
    "> [!WARNING]\n> a callout\n\n> [!TIP] Titled\n> body\n\n"
    "```cpp\nint main() { return 0; }\n```\n\n"
    "| a | b |\n| --- | --- |\n| 1 | 2 |\n\n"
    "---\n\nA [link](https://example.com) and a [[wikilink]].\n\n"
    "![a picture that does not exist](nowhere.png)\n";
  PdfRequest request = requestOf("Everything", body);
  request.wikiLinkResolves = [](std::string_view) { return false; };
  const std::string file = exported(request);
  MICRONOTES_REQUIRE(file.size() > 1000);
  MICRONOTES_REQUIRE(file.find("%%EOF") != std::string::npos);
}

// A link in an exported note is a link, not a picture of one.
//
// That was TD-42, and what it cost was every link in every exported note: a
// `[label](https://…)` reached the page as underlined accent-coloured text
// whose target appeared nowhere in the file at all -- not in an annotation,
// not beside the label -- so a reader could neither follow it nor see where it
// went. An index note, which is the shape a notes library grows, exported as a
// table of labels.
MICRONOTES_TEST(pdf_export_makes_a_link_clickable) {
  const std::string bytes =
    exported(requestOf("Links", "Read [the manual](https://example.com/manual) first.\n"));
  MICRONOTES_REQUIRE(linkCount(bytes) == 1);
  MICRONOTES_REQUIRE(bytes.find("/S /URI") != std::string::npos);
  MICRONOTES_REQUIRE(bytes.find("(https://example.com/manual)") != std::string::npos);
  // The rule under the label is already drawn in the content stream, so the
  // annotation asks for no border of its own: a viewer's default is a black
  // box around every link on the page.
  MICRONOTES_REQUIRE(bytes.find("/Border [ 0 0 0 ]") != std::string::npos);
}

// A link inside a table cell too, which is the case a second painter would
// have missed -- and a cell is where an index note keeps its links.
MICRONOTES_TEST(pdf_export_makes_a_link_in_a_table_clickable) {
  const std::string bytes = exported(
    requestOf("Index", "| Note | Where |\n| --- | --- |\n| One | [site](https://example.com) |\n"));
  MICRONOTES_REQUIRE(linkCount(bytes) == 1);
  MICRONOTES_REQUIRE(bytes.find("(https://example.com)") != std::string::npos);
}

// A note with nothing to link to carries no `/Annots` at all. An empty array
// is legal and says the same thing as its absence, and the absence is what a
// reader of the bytes should see.
MICRONOTES_TEST(pdf_export_writes_no_annotations_for_a_note_with_no_links) {
  const std::string bytes = exported(requestOf("Plain", "Just words, and no links at all.\n"));
  MICRONOTES_REQUIRE(linkCount(bytes) == 0);
  MICRONOTES_REQUIRE(bytes.find("/Annots") == std::string::npos);
}

// Only the targets that mean the same thing on someone else's machine.
//
// A relative path names a file inside *this* library and a `[[wikilink]]`
// names a note in it; neither is anywhere a reader of the PDF can go, and a
// `file:` URI into somebody else's home directory is worse than no link. This
// is the same question `followLinkAt` asks before handing a target to the
// desktop, so the page offers exactly the links the reading pane follows.
MICRONOTES_TEST(pdf_export_does_not_link_a_target_only_this_machine_can_reach) {
  const std::string local =
    exported(requestOf("Local", "See [the other note](work/other.md) and [[Some Note]].\n"));
  MICRONOTES_REQUIRE(linkCount(local) == 0);
}

// One annotation per *run*, and a link broken over two lines is two runs.
//
// Not one rect spanning both: the two halves are in different places on the
// page, and a rect around both of them would make the whitespace to the right
// of the first line and the left of the second clickable as well.
MICRONOTES_TEST(pdf_export_links_each_line_of_a_wrapped_label) {
  std::string label;
  for(int i = 0; i < 30; ++i) label += "word ";
  const std::string bytes =
    exported(requestOf("Wrapped", "A [" + label + "](https://example.com) link.\n"));
  MICRONOTES_REQUIRE(linkCount(bytes) > 1);
}

MICRONOTES_TEST(pdf_export_refuses_a_request_with_no_notes) {
  PdfRequest request;
  std::string bytes;
  const PdfResult result = micronotes::exporting::renderPdf(request, &bytes);
  MICRONOTES_REQUIRE(!result.ok);
  MICRONOTES_REQUIRE(!result.error.empty());
  MICRONOTES_REQUIRE(bytes.empty());
}

MICRONOTES_TEST(pdf_export_writes_the_file_it_was_given) {
  micronotes::tests::TempDir dir("pdf-write");
  std::filesystem::create_directories(dir.path());
  const auto file = dir.path() / "note.pdf";
  const PdfResult result =
    micronotes::exporting::writePdf(requestOf("A note", "Some text.\n"), file);
  MICRONOTES_REQUIRE(result.ok);
  MICRONOTES_REQUIRE(std::filesystem::exists(file));
  MICRONOTES_REQUIRE(std::filesystem::file_size(file) > 1000);
}

MICRONOTES_TEST(pdf_date_is_the_form_the_format_asks_for) {
  const std::string date = micronotes::exporting::pdfDate(0);
  MICRONOTES_REQUIRE(date.rfind("D:", 0) == 0);
  MICRONOTES_REQUIRE(date.size() == 16);
}

// The half of an export that runs after the file chooser closes.
//
// The chooser itself is SDL's and cannot be opened from a test, but the
// handoff can: the callback's job is to leave a path and a phase behind, and
// this is what the loop then does with them. It is the piece with a second
// thread in it, so it is the piece worth pinning.
MICRONOTES_TEST(pdf_export_writes_what_the_file_chooser_handed_back) {
  micronotes::app::UiRuntime ui;
  micronotes::tests::TempDir dir("pdf-pending");
  std::filesystem::create_directories(dir.path());

  // Nothing in flight: the loop must not do work on a pass with no export.
  MICRONOTES_REQUIRE(!micronotes::app::applyPendingExport(ui));

  ui.pdfExport->request = requestOf("A note", "Some text.\n");
  ui.pdfExport->what = "A note";
  // A name with no extension, which is what a chooser that does not enforce
  // its own filter hands back.
  ui.pdfExport->path = (dir.path() / "chosen").string();
  ui.pdfExport->phase.store(micronotes::app::PdfExportState::Phase::Chosen);

  MICRONOTES_REQUIRE(micronotes::app::applyPendingExport(ui));
  MICRONOTES_REQUIRE(std::filesystem::exists(dir.path() / "chosen.pdf"));
  // And the state is clear, so the next export can open a chooser of its own.
  MICRONOTES_REQUIRE(!ui.pdfExport->busy());
  MICRONOTES_REQUIRE(!micronotes::app::applyPendingExport(ui));
  MICRONOTES_REQUIRE(ui.status.text.find("Exported") != std::string::npos);
}

MICRONOTES_TEST(pdf_export_cancelled_at_the_chooser_writes_nothing) {
  micronotes::app::UiRuntime ui;
  ui.pdfExport->request = requestOf("A note", "Some text.\n");
  ui.pdfExport->phase.store(micronotes::app::PdfExportState::Phase::Cancelled);

  MICRONOTES_REQUIRE(micronotes::app::applyPendingExport(ui));
  MICRONOTES_REQUIRE(!ui.pdfExport->busy());
  MICRONOTES_REQUIRE(ui.pdfExport->request.notes.empty());
}

// While the chooser is up there is nothing to do. The loop calls this on every
// pass, so the waiting state has to be free and has to leave the request
// alone -- it is the answer to a question the reader has not answered yet.
MICRONOTES_TEST(pdf_export_does_nothing_while_the_chooser_is_open) {
  micronotes::app::UiRuntime ui;
  ui.pdfExport->request = requestOf("A note", "Some text.\n");
  ui.pdfExport->phase.store(micronotes::app::PdfExportState::Phase::Asking);
  MICRONOTES_REQUIRE(!micronotes::app::applyPendingExport(ui));
  MICRONOTES_REQUIRE(ui.pdfExport->busy());
  MICRONOTES_REQUIRE(ui.pdfExport->request.notes.size() == 1);
}

// A notebook's export covers its sub-notebooks too. Exporting a folder that
// stopped at its own direct children would silently leave notes out of a file
// whose whole purpose is to be the notebook.
MICRONOTES_TEST(pdf_export_of_a_folder_reaches_its_sub_notebooks) {
  micronotes::app::UiRuntime ui;
  micronotes::tests::ScratchNote scratch(ui, "pdf-folder", "The root note.\n");
  std::filesystem::create_directories(scratch.root() / "outer" / "inner");
  const auto noteIn = [&ui](const std::filesystem::path& folder, const std::string& title) {
    ui.state.selectFolder(folder);
    micronotes::tests::require(micronotes::app::createNote(ui, title),
                               "could not create " + title);
  };
  noteIn("outer", "Outer note");
  noteIn(std::filesystem::path("outer") / "inner", "Inner note");
  ui.state.refreshLibrary();

  MICRONOTES_REQUIRE(micronotes::app::notesUnderFolder(ui, "outer") == 2);
  MICRONOTES_REQUIRE(micronotes::app::notesUnderFolder(ui, std::filesystem::path("outer") / "inner") == 1);
  // The empty path is the library, which is every note in it.
  MICRONOTES_REQUIRE(micronotes::app::notesUnderFolder(ui, {}) == 3);
  // And a name that is a prefix of another folder's does not swallow it: the
  // match is by path component, not by string prefix.
  std::filesystem::create_directories(scratch.root() / "out");
  noteIn("out", "Out note");
  ui.state.refreshLibrary();
  MICRONOTES_REQUIRE(micronotes::app::notesUnderFolder(ui, "out") == 1);
  MICRONOTES_REQUIRE(micronotes::app::notesUnderFolder(ui, "outer") == 2);
}
