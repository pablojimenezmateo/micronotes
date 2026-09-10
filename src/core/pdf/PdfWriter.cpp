#include "core/pdf/PdfWriter.h"

#include "core/pdf/Deflate.h"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace microcore::pdf {
namespace {

// Two decimal places is a hundredth of a point, which at 72 points to the inch
// is a quarter of a thousandth of a millimetre. Nothing in a page of text is
// placed more precisely than that, and the digits past it are file size.
constexpr int kDecimals = 2;

}

std::string number(double value) {
  if(!std::isfinite(value)) return "0";
  std::array<char, 40> buffer {};
  const auto result =
    std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed,
                  kDecimals);
  if(result.ec != std::errc {}) return "0";
  std::string text(buffer.data(), result.ptr);
  // `to_chars` writes the requested precision exactly, so trim what it padded.
  if(text.find('.') != std::string::npos) {
    while(!text.empty() && text.back() == '0') text.pop_back();
    if(!text.empty() && text.back() == '.') text.pop_back();
  }
  // `-0` is a number no reader minds and every diff does.
  if(text == "-0" || text.empty()) return "0";
  return text;
}

std::string literalString(std::string_view value) {
  std::string out = "(";
  for(const char byte : value) {
    if(byte == '(' || byte == ')' || byte == '\\') out.push_back('\\');
    out.push_back(byte);
  }
  out.push_back(')');
  return out;
}

std::string name(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out = "/";
  for(const char byte : value) {
    const auto code = static_cast<std::uint8_t>(byte);
    const bool safe = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') ||
                      (code >= '0' && code <= '9') || code == '-' || code == '_' || code == '+';
    if(safe) {
      out.push_back(byte);
      continue;
    }
    out.push_back('#');
    out.push_back(kHex[code >> 4]);
    out.push_back(kHex[code & 0x0F]);
  }
  return out;
}

PdfWriter::PdfWriter() {
  // 1.7 because the OpenType font streams an OTF is embedded through are a 1.6
  // feature. The binary comment on the second line is the convention that tells
  // a transport the file is not text and must not have its line endings
  // rewritten -- which is exactly what would corrupt an embedded font.
  out_ = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
}

ObjectId PdfWriter::reserve() {
  offsets_.push_back(0);
  return static_cast<ObjectId>(offsets_.size());
}

void PdfWriter::beginObject(ObjectId id) {
  if(id <= 0 || static_cast<std::size_t>(id) > offsets_.size()) return;
  offsets_[static_cast<std::size_t>(id) - 1] = out_.size();
  out_ += std::to_string(id);
  out_ += " 0 obj\n";
}

void PdfWriter::object(ObjectId id, std::string_view dictEntries) {
  beginObject(id);
  out_ += "<< ";
  out_ += dictEntries;
  out_ += " >>\nendobj\n";
}

void PdfWriter::stream(ObjectId id, std::string_view dictEntries, std::string_view data,
                       bool compress) {
  std::string payload;
  bool compressed = false;
  if(compress) {
    payload = deflate(data);
    // Compression that made the data bigger is not compression. Short streams
    // -- a one-line page, a tiny image -- reach this, and storing them as they
    // came keeps the file smaller and the object simpler.
    compressed = payload.size() < data.size();
  }
  const std::string_view body = compressed ? std::string_view(payload) : data;

  beginObject(id);
  out_ += "<< ";
  out_ += dictEntries;
  if(!dictEntries.empty() && dictEntries.back() != ' ') out_ += " ";
  if(compressed) out_ += "/Filter /FlateDecode ";
  out_ += "/Length ";
  out_ += std::to_string(body.size());
  out_ += " >>\nstream\n";
  out_ += body;
  out_ += "\nendstream\nendobj\n";
}

std::string PdfWriter::finish(ObjectId catalog, ObjectId info) {
  const std::size_t xrefAt = out_.size();
  const std::size_t count = offsets_.size() + 1;
  out_ += "xref\n0 ";
  out_ += std::to_string(count);
  out_ += "\n";
  // Entry zero is the head of the free list, and its form is fixed by the
  // format. Every entry is exactly twenty bytes including the line ending,
  // which is why the offsets below are zero-padded to ten digits.
  out_ += "0000000000 65535 f \n";
  for(const std::size_t offset : offsets_) {
    std::string digits = std::to_string(offset);
    digits.insert(0, 10 - std::min<std::size_t>(digits.size(), 10), '0');
    out_ += digits;
    // An id that was reserved and never written is free rather than in use. It
    // should not happen, and a file that claims an unwritten object is at byte
    // zero is a file a reader rejects with no idea why.
    out_ += offset == 0 ? " 65535 f \n" : " 00000 n \n";
  }
  out_ += "trailer\n<< /Size ";
  out_ += std::to_string(count);
  out_ += " /Root ";
  out_ += std::to_string(catalog);
  out_ += " 0 R";
  if(info > 0) {
    out_ += " /Info ";
    out_ += std::to_string(info);
    out_ += " 0 R";
  }
  out_ += " >>\nstartxref\n";
  out_ += std::to_string(xrefAt);
  out_ += "\n%%EOF\n";
  return std::move(out_);
}

}
