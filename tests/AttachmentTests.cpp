#include "CoreAliases.h"
#include "TestSupport.h"
#include "TempDir.h"

#include "core/attachments/AttachmentService.h"

#include <filesystem>
#include <fstream>

MICRONOTES_TEST(attachment_service_detects_supported_images) {
  microcore::attachments::AttachmentService service;
  MICRONOTES_REQUIRE(service.isSupportedImage("photo.PNG"));
  MICRONOTES_REQUIRE(!service.isSupportedImage("document.pdf"));
}

MICRONOTES_TEST(attachment_service_copies_and_links_files) {
  const micronotes::tests::TempDir rootDir("micronotes-attachment-test");
  const micronotes::tests::TempDir sourceTemp("micronotes-attachment-source");
  const auto& root = rootDir.path();
  const auto& sourceDir = sourceTemp.path();
  std::filesystem::create_directories(sourceDir);
  const auto source = sourceDir / "image.png";
  {
    std::ofstream out(source);
    out << "png";
  }
  microcore::attachments::AttachmentService service;
  const auto link = service.attachFile(root, "note-1", source);
  MICRONOTES_REQUIRE(link.image);
  MICRONOTES_REQUIRE(link.markdown.find("![image.png](") == 0);
  MICRONOTES_REQUIRE(std::filesystem::exists(link.managedPath));
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(attachment_service_labels_non_image_links_with_file_name) {
  const micronotes::tests::TempDir rootDir("micronotes-attachment-label-test");
  const micronotes::tests::TempDir sourceTemp("micronotes-attachment-label-source");
  const auto& root = rootDir.path();
  const auto& sourceDir = sourceTemp.path();
  std::filesystem::create_directories(sourceDir);
  const auto source = sourceDir / "From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf";
  {
    std::ofstream out(source);
    out << "pdf";
  }
  microcore::attachments::AttachmentService service;
  const auto link = service.attachFile(root, "note-1", source);
  MICRONOTES_REQUIRE(!link.image);
  MICRONOTES_REQUIRE(link.markdown.find("[From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf](") == 0);
  MICRONOTES_REQUIRE(std::filesystem::exists(link.managedPath));
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(attachment_service_writes_clipboard_bytes_with_unique_names) {
  const micronotes::tests::TempDir rootDir("micronotes-attach-bytes-test");
  const auto& root = rootDir.path();
  const char first[] = "first";
  const char second[] = "second";

  microcore::attachments::AttachmentService service;
  const auto one = service.attachBytes(root, "note-1", "clipboard.png", first, sizeof(first) - 1);
  const auto two = service.attachBytes(root, "note-1", "clipboard.png", second, sizeof(second) - 1);

  MICRONOTES_REQUIRE(one.image);
  MICRONOTES_REQUIRE(two.image);
  MICRONOTES_REQUIRE(one.managedPath != two.managedPath);
  MICRONOTES_REQUIRE(std::filesystem::exists(one.managedPath));
  MICRONOTES_REQUIRE(std::filesystem::exists(two.managedPath));
  MICRONOTES_REQUIRE(two.markdown.find("clipboard-2.png") != std::string::npos);
}

MICRONOTES_TEST(attachment_service_builds_default_open_command) {
  const micronotes::tests::TempDir rootDir("micronotes-open-test");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root / ".micronotes" / "attachments" / "note");
  const auto file = root / ".micronotes" / "attachments" / "note" / "doc.pdf";
  {
    std::ofstream out(file);
    out << "pdf";
  }
  microcore::attachments::AttachmentService service;
  const auto command = service.openCommand(root, ".micronotes/attachments/note/doc.pdf");
  MICRONOTES_REQUIRE(command.size() == 2);
  MICRONOTES_REQUIRE(command[0] == "xdg-open");
}

MICRONOTES_TEST(attachment_service_rejects_path_traversal) {
  const micronotes::tests::TempDir rootDir("micronotes-attachment-boundary");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root);
  microcore::attachments::AttachmentService service;
  bool rejected = false;
  try {
    (void)service.resolveManaged(root, "../outside.pdf");
  } catch(...) {
    rejected = true;
  }
  MICRONOTES_REQUIRE(rejected);
}
