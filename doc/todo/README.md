# Issue / TODO tracker

This folder is a lightweight file-based issue tracker for the project.
Each issue is one Markdown file.

## File naming

`NNN-short-kebab-case-title.md` — e.g. `001-key-sequences-no-end-state-check.md`.
`NNN` is a zero-padded sequence number; take the next free one. The number
never changes, even if the title is later reworded.

## File structure

Each issue file starts with a YAML frontmatter block:

```yaml
---
id: 001
title: One-line summary of the issue
type: bug | enhancement | task
status: open | in-progress | done | wontfix
priority: high | medium | low
component: keyboard | display | modbus | wifi | ...
created: YYYY-MM-DD
resolved: YYYY-MM-DD   # only when status is done/wontfix
---
```

followed by these sections (omit those that do not apply):

- **Summary** — what is wrong / what is wanted, a few sentences.
- **Observed behavior** — what actually happened, with log excerpts,
  timestamps, affected files (for bugs).
- **Expected behavior** — what should have happened.
- **Root cause** — analysis result, with code references (`file.cpp:line`).
- **Proposed fix** — suggested approach, alternatives, open questions.
- **Resolution** — filled in when closing: what was done, commit reference.

## Rules

- English only.
- No Wi-Fi credentials, local secrets or private LAN IPs — use placeholders
  like `<boiler-ip>`, `192.168.x.x`.
- Closed issues stay in this folder with `status: done`; do not delete them.
