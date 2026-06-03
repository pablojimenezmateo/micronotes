#include "TestSupport.h"

#include "attachments/AttachmentService.h"

#include <filesystem>
#include <fstream>

MICRONOTES_TEST(attachment_service_detects_supported_images) {
  micronotes::attachments::AttachmentService service;
  MICRONOTES_REQUIRE(service.isSupportedImage("photo.PNG"));
  MICRONOTES_REQUIRE(!service.isSupportedImage("document.pdf"));
}

MICRONOTES_TEST(attachment_service_copies_and_links_files) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-attachment-test";
  const auto sourceDir = std::filesystem::temp_directory_path() / "micronotes-attachment-source";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
  std::filesystem::create_directories(sourceDir);
  const auto source = sourceDir / "image.png";
  {
    std::ofstream out(source);
    out << "png";
  }
  micronotes::attachments::AttachmentService service;
  const auto link = service.attachFile(root, "note-1", source);
  MICRONOTES_REQUIRE(link.image);
  MICRONOTES_REQUIRE(link.markdown.find("![image.png](") == 0);
  MICRONOTES_REQUIRE(std::filesystem::exists(link.managedPath));
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
}

MICRONOTES_TEST(attachment_service_labels_non_image_links_with_file_name) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-attachment-label-test";
  const auto sourceDir = std::filesystem::temp_directory_path() / "micronotes-attachment-label-source";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
  std::filesystem::create_directories(sourceDir);
  const auto source = sourceDir / "From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf";
  {
    std::ofstream out(source);
    out << "pdf";
  }
  micronotes::attachments::AttachmentService service;
  const auto link = service.attachFile(root, "note-1", source);
  MICRONOTES_REQUIRE(!link.image);
  MICRONOTES_REQUIRE(link.markdown.find("[From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf](") == 0);
  MICRONOTES_REQUIRE(std::filesystem::exists(link.managedPath));
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
}

MICRONOTES_TEST(attachment_service_writes_clipboard_bytes_with_unique_names) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-attach-bytes-test";
  std::filesystem::remove_all(root);
  const char first[] = "first";
  const char second[] = "second";

  micronotes::attachments::AttachmentService service;
  const auto one = service.attachBytes(root, "note-1", "clipboard.png", first, sizeof(first) - 1);
  const auto two = service.attachBytes(root, "note-1", "clipboard.png", second, sizeof(second) - 1);

  MICRONOTES_REQUIRE(one.image);
  MICRONOTES_REQUIRE(two.image);
  MICRONOTES_REQUIRE(one.managedPath != two.managedPath);
  MICRONOTES_REQUIRE(std::filesystem::exists(one.managedPath));
  MICRONOTES_REQUIRE(std::filesystem::exists(two.managedPath));
  MICRONOTES_REQUIRE(two.markdown.find("clipboard-2.png") != std::string::npos);
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(attachment_service_builds_default_open_command) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-open-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / ".micronotes" / "attachments" / "note");
  const auto file = root / ".micronotes" / "attachments" / "note" / "doc.pdf";
  {
    std::ofstream out(file);
    out << "pdf";
  }
  micronotes::attachments::AttachmentService service;
  const auto command = service.openCommand(root, ".micronotes/attachments/note/doc.pdf");
  MICRONOTES_REQUIRE(command.size() == 2);
  MICRONOTES_REQUIRE(command[0] == "xdg-open");
  std::filesystem::remove_all(root);
}

MICRONOTES_TEST(attachment_service_rejects_path_traversal) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-attachment-boundary";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  micronotes::attachments::AttachmentService service;
  bool rejected = false;
  try {
    (void)service.resolveManaged(root, "../outside.pdf");
  } catch(...) {
    rejected = true;
  }
  MICRONOTES_REQUIRE(rejected);
  std::filesystem::remove_all(root);
}
