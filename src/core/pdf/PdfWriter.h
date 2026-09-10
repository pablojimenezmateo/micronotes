#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The file format itself: numbered objects, streams, the cross-reference table
// and the trailer. It knows nothing about pages, fonts or text -- those are
// `PdfDocument`, which is written in terms of this.
//
// The split is the one every writer here needs: an object's *body* is built by
// whoever knows what the object means, and the byte offsets, the `/Length` and
// the xref are bookkeeping that is the same for all of them and is wrong in a
// way nothing notices until a reader rejects the file.
namespace microcore::pdf {

// A PDF object number. Zero is never a real object -- the format reserves it
// for the head of the free list -- so it doubles as "no object".
using ObjectId = int;

// A number as a PDF writes one: no exponent, no locale, and no trailing zeros.
//
// `std::to_string` is locale-dependent for a double, which on a machine set to
// a comma decimal separator writes `12,5` into the content stream and produces
// a file no reader will open. This has bitten every PDF writer ever written.
std::string number(double value);

// A PDF literal string, escaped. For the document information dictionary and
// anywhere else free text lands in the file rather than in a content stream.
std::string literalString(std::string_view value);

// A PDF name (`/Foo`), with anything outside the safe set hex-escaped.
std::string name(std::string_view value);

class PdfWriter {
public:
  PdfWriter();

  // An object number, to be written later. Reserving up front is what lets an
  // object refer to one that has not been written yet -- a page has to name
  // its content stream, and the pages tree has to name every page in it.
  ObjectId reserve();

  // A dictionary object. `dictEntries` is its contents *without* the enclosing
  // `<<` and `>>`, which is the same convention `stream` below takes -- every
  // object a PDF of this shape contains is a dictionary, and two spellings of
  // one would be two chances to leave the braces off.
  void object(ObjectId id, std::string_view dictEntries);

  // A stream object. `dictEntries` is as above, and without `/Length`, which
  // is filled in here because it is a property of the bytes rather than of the
  // caller's intent.
  //
  // `compress` runs the data through `FlateDecode`. It is a request, not a
  // promise: a stream that grows under compression is stored as it was, which
  // is what makes it safe to pass `true` for everything.
  void stream(ObjectId id, std::string_view dictEntries, std::string_view data, bool compress);

  // The xref table and the trailer. `catalog` is the document catalog and
  // `info` its information dictionary, or 0 for none. Nothing may be written
  // after this.
  std::string finish(ObjectId catalog, ObjectId info);

private:
  void beginObject(ObjectId id);

  std::string out_;
  // Byte offset of each object, indexed by `id - 1`. Zero means an id was
  // reserved and never written, which `finish` reports as a free entry rather
  // than as an offset into the middle of some other object.
  std::vector<std::size_t> offsets_;
};

}
