#pragma once

#include <string>
#include <string_view>

// The one compressor the PDF writer uses, and the reason it is a unit of its
// own: `FlateDecode` is the only stream filter a PDF needs, and whether it is
// real compression or not is a build-time question that nothing above here
// should have to ask.
//
// With zlib present this is zlib. Without it, the output is still a valid zlib
// stream -- deflate's *stored* block type, which is the format's own way of
// saying "this run did not compress" -- so a PDF written on a machine without
// zlib is larger and identical in every other respect. That fallback is not a
// stub: an embedded font file and a decoded PNG both have to go through
// `FlateDecode` whatever is installed, and a writer that could only sometimes
// produce one would be a writer with two output formats.
namespace microcore::pdf {

// A zlib stream, ready to be the body of a `/Filter /FlateDecode` object.
std::string deflate(std::string_view data);

// Whether real compression is compiled in. For the diagnostic that reports it,
// and for the test that asserts the two paths agree on what they produce.
bool deflateCompresses();

}
