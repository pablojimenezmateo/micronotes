# product-vision Specification

## Purpose
TBD - created by archiving change bootstrap-micronotes. Update Purpose after archive.
## Requirements
### Requirement: Native Linux Desktop Application
The system SHALL provide a native Linux desktop notes application built with C++ and SDL3.

#### Scenario: Launch on supported platform
- **WHEN** the user starts `micronotes` on Linux with required runtime dependencies installed
- **THEN** the system opens a single application window without requiring a network connection

#### Scenario: Unsupported platform excluded
- **WHEN** implementation or packaging work targets Windows, macOS, mobile, or web
- **THEN** the system treats that work as outside the initial product scope

### Requirement: Local-Only Operation
The system SHALL operate without internet access and MUST NOT require network services for note creation, editing, search, viewing, or attachment access.

#### Scenario: Network unavailable
- **WHEN** the machine has no active network connection
- **THEN** the user can open a local library, edit notes, view rendered Markdown, search indexed content, and open local attachments

### Requirement: Minimal Scope
The system SHALL exclude sync, cloud accounts, telemetry, plugin marketplaces, web clipper features, WYSIWYG editing, importers, and renderer extensions such as Mermaid.

#### Scenario: Excluded feature requested by runtime data
- **WHEN** a note contains Mermaid syntax, plugin directives, or app-specific importer metadata
- **THEN** the system renders or displays it as ordinary Markdown text where possible and does not execute extension behavior

### Requirement: Offline Dependency Posture
The system SHALL build from repository-local source plus Linux system development packages and MUST NOT fetch dependencies during normal configure or build steps.

#### Scenario: Configure build offline
- **WHEN** a developer configures the project on a Linux machine with required system packages and vendored source present
- **THEN** CMake completes dependency discovery without downloading files from the internet

