#include "core/pdf/PdfDocument.h"

#include <utility>

namespace microcore::pdf {

namespace {

// The page's `/Annots` array, with the annotation objects written out beside
// it -- or nothing at all, which is what a page with no links on it gets. An
// empty `/Annots []` is legal and says the same thing as its absence, so the
// absence is what is written.
//
// `/Border [0 0 0]` because the rule under the label is already drawn in the
// content stream: a viewer's own default border is a black box around every
// link on the page.
std::string annotationArray(PdfWriter& writer,
                            const std::vector<PdfDocument::Annotation>& annotations,
                            float pageHeight) {
  std::string ids;
  for(const auto& annotation : annotations) {
    if(annotation.uri.empty()) continue;
    if(annotation.width <= 0.0f || annotation.height <= 0.0f) continue;
    // Top-down y to the PDF's bottom-up y. The one place in this file where
    // the two disagree; `PdfContent::flip` is the other, for the stream.
    const float bottom = pageHeight - (annotation.y + annotation.height);
    const ObjectId id = writer.reserve();
    std::string body = "/Type /Annot /Subtype /Link /Border [ 0 0 0 ] ";
    body += "/Rect [ " + number(annotation.x) + " " + number(bottom) + " " +
            number(annotation.x + annotation.width) + " " + number(bottom + annotation.height) +
            " ] ";
    body += "/A << /Type /Action /S /URI /URI " + literalString(annotation.uri) + " >>";
    writer.object(id, body);
    ids += " " + std::to_string(id) + " 0 R";
  }
  if(ids.empty()) return {};
  return " /Annots [" + ids + " ]";
}

}

int PdfDocument::addFont(SfntFont font, std::string baseName) {
  fonts_.emplace_back(std::move(font), std::move(baseName));
  return static_cast<int>(fonts_.size()) - 1;
}

int PdfDocument::addImage(PdfImage image) {
  images_.push_back(std::move(image));
  return static_cast<int>(images_.size()) - 1;
}

void PdfDocument::addPage(float width, float height, std::string content,
                          std::vector<Annotation> annotations) {
  pages_.push_back({width, height, std::move(content), std::move(annotations)});
}

std::string PdfDocument::finish(const Info& info) {
  PdfWriter writer;
  const ObjectId catalog = writer.reserve();
  const ObjectId pageTree = writer.reserve();
  const ObjectId resources = writer.reserve();

  std::vector<ObjectId> pageIds;
  pageIds.reserve(pages_.size());
  for(std::size_t i = 0; i < pages_.size(); ++i) pageIds.push_back(writer.reserve());

  // The pages themselves first, so every font has been asked to encode
  // everything it will ever be asked to encode before it is written.
  for(std::size_t i = 0; i < pages_.size(); ++i) {
    const ObjectId content = writer.reserve();
    const Page& page = pages_[i];
    std::string body = "/Type /Page /Parent " + std::to_string(pageTree) + " 0 R ";
    body += "/MediaBox [ 0 0 " + number(page.width) + " " + number(page.height) + " ] ";
    body += "/Contents " + std::to_string(content) + " 0 R";
    body += annotationArray(writer, page.annotations, page.height);
    writer.object(pageIds[i], body);
    writer.stream(content, "", page.content, true);
  }

  std::string fontResources;
  for(int slot = 0; slot < static_cast<int>(fonts_.size()); ++slot) {
    // A face nothing set a word in is left out entirely rather than embedded
    // and never used. A note with no code in it should not carry a monospace
    // font file it never shows a character of.
    if(!fonts_[static_cast<std::size_t>(slot)].used()) continue;
    const ObjectId id = fonts_[static_cast<std::size_t>(slot)].write(writer);
    fontResources += "/F" + std::to_string(slot) + " " + std::to_string(id) + " 0 R ";
  }

  std::string imageResources;
  for(int slot = 0; slot < static_cast<int>(images_.size()); ++slot) {
    const ObjectId id = writeImage(writer, images_[static_cast<std::size_t>(slot)]);
    if(id == 0) continue;
    imageResources += "/Im" + std::to_string(slot) + " " + std::to_string(id) + " 0 R ";
  }

  std::string resourceBody = "/ProcSet [ /PDF /Text /ImageB /ImageC ]";
  if(!fontResources.empty()) resourceBody += " /Font << " + fontResources + ">>";
  if(!imageResources.empty()) resourceBody += " /XObject << " + imageResources + ">>";
  writer.object(resources, resourceBody);

  std::string treeBody = "/Type /Pages /Count " + std::to_string(pageIds.size()) + " /Kids [";
  for(const ObjectId id : pageIds) treeBody += " " + std::to_string(id) + " 0 R";
  treeBody += " ] /Resources " + std::to_string(resources) + " 0 R";
  writer.object(pageTree, treeBody);

  writer.object(catalog, "/Type /Catalog /Pages " + std::to_string(pageTree) + " 0 R");

  ObjectId infoId = 0;
  std::string infoBody;
  if(!info.title.empty()) infoBody += "/Title " + literalString(info.title) + " ";
  if(!info.author.empty()) infoBody += "/Author " + literalString(info.author) + " ";
  if(!info.creator.empty()) {
    infoBody += "/Creator " + literalString(info.creator) + " ";
    infoBody += "/Producer " + literalString(info.creator) + " ";
  }
  if(!info.creationDate.empty()) {
    infoBody += "/CreationDate " + literalString(info.creationDate) + " ";
  }
  if(!infoBody.empty()) {
    infoId = writer.reserve();
    writer.object(infoId, infoBody);
  }

  return writer.finish(catalog, infoId);
}

}
