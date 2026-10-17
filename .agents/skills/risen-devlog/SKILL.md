---
name: risen-devlog
description: Write entries in risen-mods TODO.md and DEVELOPMENT_LOG.md in the project's short format. Use after finishing any action, test, deployment or user report, and when adding or closing a TODO item.
---

# TODO.md and DEVELOPMENT_LOG.md

Both files are read by every agent session - keep them short; details belong in `notes/*.md`.

## DEVELOPMENT_LOG.md
- Section `## YYYY-MM-DD`, newest day on top; entries inside a day in ascending time order.
- Entry: `- HH:MM — <what was done>. <check result>. <state left>.` Local time Europe/Kaliningrad (UTC+02:00);
  never invent times of past sessions.
- 1-3 sentences, about 300-500 characters. Include only: what changed, how it was verified (counts, not lists),
  which build/backup is installed (timestamp, backup id), what is pending for the user.
- Mark clearly: prepared test vs user-confirmed result. Quote the user's verdict briefly; don't generalize it.
- Don't repeat unchanged facts (DLLs that stayed the same, "no commit/push") in every entry.

## TODO.md
- Plan + findings. Item: `- [ ] <goal>` with indented lines: key findings, limits, evidence path, next step.
- Close with `- [x]` plus a short "how" (2-4 lines) and the date; move long reasoning into notes.
- Open problems (crashes, failed tests) stay visible until resolved; never delete or soften them.
- One item per user-visible feature/test; sub-items `  - [ ]` for steps.
- A new feature without a TODO item is a bug: add it when you start coding, not after.

## Before writing
`git status --short` - keep others' unfinished changes. Verify facts (file timestamps, hashes, log lines)
instead of copying old entries.
