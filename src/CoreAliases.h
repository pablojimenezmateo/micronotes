#pragma once

// The shared core lives in namespace microcore so that micronotes and
// microagenda can vendor byte-identical copies of src/core (see
// tools/sync-core.sh). App code, however, reads better saying platform::,
// perf::, markdown:: than microcore::platform:: on every line -- and those
// unqualified names already resolved that way before the core was split out.
//
// Aliasing the core subsystems into namespace micronotes keeps that spelling
// working: unqualified lookup from micronotes::library, micronotes::ui, and
// micronotes::app walks out to the enclosing namespace and finds these.
//
// The empty namespace definitions below just make the names declarable without
// dragging in the core headers; each translation unit still includes whichever
// core headers it actually uses.

namespace microcore::attachments {}
namespace microcore::editor {}
namespace microcore::markdown {}
namespace microcore::perf {}
namespace microcore::persistence {}
namespace microcore::platform {}
namespace microcore::ui {}
namespace microcore::viewer {}

namespace micronotes {

namespace attachments = microcore::attachments;
namespace editor = microcore::editor;
namespace markdown = microcore::markdown;
namespace perf = microcore::perf;
namespace persistence = microcore::persistence;
namespace platform = microcore::platform;
namespace viewer = microcore::viewer;

}
