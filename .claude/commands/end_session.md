Review the work done in this session using git diff and the session notes in memory, then update all relevant documentation files:

1. **Session_Log.md** — add a dated entry summarising what changed (features, fixes, refactors). Use the same format as existing entries.

2. **CHANGELOG.md** — add entries under the current version for user-visible changes. Skip internal refactors that don't affect behaviour.

3. **man pages** (`man1/`) — update any man page whose described feature or option changed. Check: `camclops.1`, `clops_gpsinfo.1`, `clops_ls.1`, `clops_probe.1`, `clops_tripdebug.1`, `clops_videos.1`, `clops_audit.1`, `clops_findgpslock.1`, `clops_gpsexport.1`. Only touch pages that need changes.

4. **ROADMAP / Next.md** — move completed items to Done, add new items discovered this session.

5. **camclops_project_brief.md** — update if the architecture, feature set, or supported cameras changed in a meaningful way.

6. **README** (if present at root) — update feature list or usage examples if user-visible behaviour changed.

7. **Memory** — update `/home/iceberg/.claude/projects/-z-Nutball-Labs-camclops/memory/MEMORY.md`:
   - Add/update session notes for today
   - **Active TODO**: remove items completed this session, update wording on items that changed scope, add new items that came up. Cross-reference the shower thought files for anything that moved from idea → active work or active work → done.
   - Update HWM if it was bumped this session.

Rules:
- Read each file before editing it.
- Do not fabricate changes — only document what actually happened in this session.
- Use the existing writing style and section format of each file.
- Keep entries concise; this is a changelog, not a novel.
- After all edits, report which files were changed and which were skipped (and why).
