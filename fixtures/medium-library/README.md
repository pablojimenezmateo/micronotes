# Medium Library Fixture

This fixture describes the baseline regression library shape used by tests and performance harnesses:

- 250 Markdown notes
- Nested folders for work, personal, and archive notes
- Tags such as `fast`, `local`, `sqlite`, and `markdown`
- Local image and non-image attachment references

The generated performance harness creates this shape under the system temporary directory so the repository does not need hundreds of committed fixture files.
