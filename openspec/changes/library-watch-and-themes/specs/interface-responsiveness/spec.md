## ADDED Requirements

### Requirement: First Paint Independent Of Library Size
The system SHALL present its window and accept input without waiting for the library scan or the search index rebuild to complete.

#### Scenario: Launch against a large library
- **WHEN** the application starts with a library containing several thousand notes
- **THEN** the window paints and accepts keyboard input before the scan finishes

#### Scenario: Sidebar during the initial scan
- **WHEN** the library scan has not yet completed
- **THEN** the sidebar reports that it is reading the library rather than appearing empty or frozen

### Requirement: Search Does Not Block Typing
The system SHALL keep the editing and search surfaces responsive to input while a search or an index rebuild is in progress.

#### Scenario: Typing a query against a large library
- **WHEN** the user types successive characters into the search field on a large library
- **THEN** each keystroke is accepted and rendered without waiting for the previous query to finish

#### Scenario: Superseded query result
- **WHEN** a search result arrives for a query the user has already changed
- **THEN** the stale result is discarded and MUST NOT replace the result for the current query

### Requirement: Steady-State Repaint Performs No Allocation
The system SHALL repaint an unchanged document without allocating, so that redraw cost does not grow with the number of distinct strings previously drawn.

#### Scenario: Repainting an unchanged note
- **WHEN** the application repaints a note whose content, theme, scale and scroll position are unchanged
- **THEN** the text cache performs no heap allocation

#### Scenario: Text cache under pressure
- **WHEN** the text cache reaches its memory budget
- **THEN** the system evicts least-recently-used entries and MUST NOT discard the entire cache

### Requirement: Background Work Reports Back Without Losing Results
The system SHALL deliver background results to the main thread through a checked wake path, so that a completed result cannot be stranded by a rejected or unregistered wake event.

#### Scenario: Wake event rejected
- **WHEN** a background result is ready but the wake event cannot be queued
- **THEN** the system records that a wake is owed and shortens its next idle wait so the result is drained

#### Scenario: Wake registration unavailable at startup
- **WHEN** the custom wake event type cannot be registered
- **THEN** the main loop falls back to a bounded wait rather than blocking indefinitely

#### Scenario: Shutdown with work in flight
- **WHEN** the application exits while background work is still running
- **THEN** outstanding work is cancelled and no completion touches released state

### Requirement: Performance Claims Are Backed By Measurement
The system SHALL define its responsiveness budgets numerically and SHALL provide a harness that measures them, so that a performance claim in documentation corresponds to a check that can fail.

#### Scenario: Re-layout budget after a keystroke
- **WHEN** the performance harness applies a keystroke to a 200 KB note
- **THEN** the measured re-layout time is reported against a stated budget

#### Scenario: Undocumented budget
- **WHEN** documentation states a responsiveness guarantee
- **THEN** a corresponding harness scenario exists that measures it
