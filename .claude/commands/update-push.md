Prepare all documentation for a release, draft GitHub release notes, then — after explicit user approval — commit, push, and publish the release.

---

## Phase 1 — Audit & Draft (always runs first)

### 1. Establish current version

Read `lib/version.hpp`. Construct the canonical version string:
`VERSION_MAJOR.VERSION_MINOR.VERSION_PATCHsuffix` (e.g. `2.0.1a`).
Also note `VERSION_HWM`. This is the ground truth for all version references.

### 2. Identify what changed since last release

Run `git log --oneline` to find the most recent release commit (look for commits matching the version tag pattern or "CamClops v" in the message). Collect all commits since that point — these drive the release notes.

Also run `git status` to see what is staged/unstaged/untracked. Note any doc files that are modified but not committed.

### 3. Audit each documentation file

Read each file before assessing it. Check only what is listed here:

**CHANGELOG.md**
- Has a section for the current version at or near the top?
- Entries cover the commits identified in step 2?
- Version string and date are correct?
- If missing or incomplete: add/complete the entry now.
- **Do NOT edit the content of historical entries.** Entries written before the
  rebrand (PathMux → CamClops) retain their original tool names (pm_*, pathmux-tl,
  etc.) — do not update them to CamClops names. The rebrand commit is the canonical
  marker. Only add new entries or correct factual errors in the current version section.

**ROADMAP.md**
- Items completed in the current cycle moved to a Done/Completed section?
- New planned items added if they came up this session?
- If stale: update now.

**ROADMAP_MacOS.md** and **ROADMAP_WINDOWS.md** (if present)
- Same check as ROADMAP.md — platform-specific completed items moved to Done.

**README.md**
- Version reference (if any) matches current version?
- Feature list reflects current capabilities (new cameras, new tools, new GUI features)?
- Installation/build instructions still accurate?
- If stale: update now.

**Session_Log.md**
- Has a dated entry for the current session's work?
- If missing: add a brief dated entry summarising what changed.

**camclops_project_brief.md**
- Reflects current architecture, phase status, and feature set?
- Only update if something materially changed (new tool, new camera, major GUI addition).

**man pages (`man1/*.1`)**
- For each man page, check whether its documented options/behaviour match the current source.
- Man pages to check: `camclops.1`, `clops_gpsinfo.1`, `clops_ls.1`, `clops_probe.1`, `clops_tripdebug.1`, `clops_videos.1`, `clops_audit.1`, `clops_findgpslock.1`, `clops_gpsexport.1`.
- Only update a page if its content is actually wrong or missing something. Don't touch pages that are accurate.

### 4. Find release packages

List `packages/` directory. Filter to files whose name contains the current version string (e.g. `camclops-2.0.1a`). Ignore temp pkgbuild files (names starting with `(`). Group by platform:
- Linux: `.rpm`, `.deb`, `.tar.gz`
- macOS: `.pkg`, `.tar.gz`, `.zip`
- Windows: `.msi`, `.zip`

Note which platform packages are present and which are missing.

### 5. Check GitHub remote

Run `git remote get-url origin` to get the actual remote URL. Derive the `OWNER/REPO` slug from it. Do NOT assume the repo is named CamClops — the rename may still be pending.

Check if a GitHub release tag already exists for the current version:
`gh release view vVERSION --repo OWNER/REPO 2>&1`

If it already exists, note that and warn the user — the push phase will need `--update` or the user may want to skip.

### 6. Draft release notes

Write a GitHub release body in this format:

```
## CamClops vVERSION

### What's new
<bullet list of user-visible changes, drawn from commits and CHANGELOG entries>

### Bug fixes
<bullet list of fixes, if any>

### Packages
| Platform | File |
|---|---|
| Linux (RPM) | camclops-VERSION-1.x86_64.rpm |
| Linux (DEB) | camclops_VERSION_amd64.deb |
| Linux (tar) | camclops-VERSION-Linux.tar.gz |
| macOS | camclops-VERSION-macOS.pkg |
| Windows | camclops-VERSION-win64.msi |
<only include rows for packages that actually exist>

### Requirements
- Linux: Alma/RHEL 9+, ffmpeg (RPM Fusion), ExifTool 13.51+
- macOS: 12+, ffmpeg + exiftool via Homebrew
- Windows: 10/11 x64, bundled installer includes ffmpeg

### Notes
<any caveats, known issues, or upgrade notes relevant to this release>
```

Replace `VERSION` with the actual version string throughout.

### 7. Present for approval

Output a clear summary:

1. **Doc audit results** — list each file: ✓ already current / ✏ updated now / ⚠ needs manual attention
2. **Packages found** — list by platform
3. **GitHub remote** — show `OWNER/REPO` and whether a release tag already exists
4. **Full draft release notes** — the complete text from step 6

Then output this exact line and stop:

```
---
Ready to push. Reply with "approved" (or "approve") to commit docs, push to GitHub, create the release, and upload packages. Reply with anything else to make changes first.
---
```

Do NOT proceed to Phase 2 until the user explicitly approves.

---

## Phase 2 — Commit, Push, Publish (only after explicit approval)

Wait for the user to say "approved" or "approve". If they say anything else, address their feedback and re-present for approval.

### 8. Commit documentation changes

If any doc files were modified in Phase 1:
- Stage only the doc files that were changed (never `git add -A`)
- Commit with message: `docs: release prep for vVERSION — CamClops vVERSION (SN HWM)`

### 9. Push to GitHub

`git push origin main`

### 10. Create GitHub release

Construct the `gh release create` command:
- Tag: `vVERSION`
- Title: `CamClops vVERSION`
- Body: the approved release notes text
- `--latest` flag to mark as latest release
- Attach all packages found in step 4

Example:
```
gh release create vVERSION \
  --repo OWNER/REPO \
  --title "CamClops vVERSION" \
  --notes-file <(echo "RELEASE_BODY") \
  --latest \
  packages/camclops-VERSION-1.x86_64.rpm \
  packages/camclops_VERSION_amd64.deb \
  packages/camclops-VERSION-Linux.tar.gz \
  packages/camclops-VERSION-macOS.pkg \
  packages/camclops-VERSION-macOS.tar.gz \
  packages/camclops-VERSION-macOS.zip \
  packages/camclops-VERSION-win64.msi \
  packages/camclops-VERSION-win64.zip
```
Only include package paths that actually exist. Use `--notes-file -` with a heredoc or write a temp file for the body.

If a release tag already exists (detected in step 5), use `gh release edit` to update it instead of `gh release create`.

### 11. Report

Output:
- GitHub release URL (returned by `gh release create`)
- List of packages uploaded
- Confirmation that `main` is pushed and release is marked latest
