#pragma once

#include <filesystem>
#include <string>

// The harness's lanes. Each one builds what it needs, prints its own table and
// returns whether its budgets held; `PerfMain.cpp` runs them in order and ORs
// the answers, which is the whole of the coupling between them.
//
// A lane per file so that "add to the shell lane" -- which
// `docs/performance.md` says in those words -- names a file rather than a line
// number in a 1,553-line one.
namespace micronotes::perfharness {

// DocumentLanes.cpp
bool layoutBudgets(std::string* out);
bool scrollBudgets(const std::string& source);
bool selectionBudgets(const std::string& source);
bool editBudgets(const std::string& source);

// InteractionLane.cpp
bool interactionBudgets(const std::string& base);

// PersistenceLane.cpp
bool persistenceBudgets(const std::filesystem::path& root, const std::string& body);

// UndoLane.cpp
bool undoBudgets();

// ShellLane.cpp
bool shellBudgets(const std::filesystem::path& root, const std::string& body);

// SearchLane.cpp
bool searchBudgets(const std::filesystem::path& root);

// FontLane.cpp
bool fontBudgets(const std::string& base);

}
