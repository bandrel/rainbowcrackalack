# Review Codebase Skill — Design Spec

## Overview

A Claude Code skill (`/review-codebase`) that performs a full codebase audit for performance enhancements and bug fixes. Three sequential agent phases — review, comment, implement — with results tracked as GitLab issues.

## Trigger

Manually invoked via `/review-codebase` in Claude Code.

## Configuration

| Setting | Source | Default |
|---------|--------|---------|
| GitLab token | `GITLAB_TOKEN` env var | (required) |
| GitLab API URL | Hardcoded | `https://git.bandrel.com/api/v4` |
| Project ID | Hardcoded | `5` (bandrel/rainbowcrackalack) |

## Phase 1: Reviewer (Haiku)

**Purpose**: Scan the full codebase for bugs and performance issues.

**Scope**: All host C code (`.c`, `.h`), OpenCL kernels (`CL/*.cl`), Metal shaders (`Metal/*.metal`).

**What it looks for**:
- **Bugs**: Off-by-one errors, out-of-bounds access, race conditions, memory leaks, null dereference, integer overflow, uninitialized variables
- **Performance**: Suboptimal GPU memory access patterns (non-coalesced reads, bank conflicts), low occupancy hints, redundant computation, unnecessary global memory traffic, missed local/shared memory caching opportunities, inefficient loop structures

**Output**: For each finding, the agent produces:
- File path and line range
- Category: `bug` or `performance`
- Severity: `critical`, `major`, `minor`
- Title (short, imperative)
- Description (what's wrong and why it matters)

**GitLab action**: Creates one issue per finding with labels:
- `auto-review`
- `bug` or `performance` (matching category)
- `severity::critical`, `severity::major`, or `severity::minor`

**Model**: Haiku — fast and cheap for broad scanning. Acceptable to have some false positives since Phase 2 filters them.

## Phase 2: Commenter (Sonnet)

**Purpose**: Triage and enrich each finding with actionable fix guidance.

**Input**: All GitLab issues created in Phase 1 (filtered by `auto-review` label).

**For each issue, the agent**:
1. Reads the referenced file and surrounding context
2. Determines if the finding is valid or a false positive
3. If **false positive**: closes the issue with a comment explaining why
4. If **valid**: adds a comment containing:
   - Root cause analysis
   - Suggested fix approach (specific enough to implement)
   - Expected impact (performance gain estimate or bug severity)
   - Risks or side effects of the fix
   - Related code that may need updating (e.g., Metal shader if OpenCL kernel changes)

**Model**: Sonnet — good balance of depth and cost for code analysis.

## Phase 3: Implementer (Opus)

**Purpose**: Implement fixes for all remaining open issues.

**Input**: All open GitLab issues with `auto-review` label (i.e., those not closed as false positives in Phase 2).

**Process**:
1. Creates branch `auto-review-YYYY-MM-DD` from current `master`
2. For each open issue (ordered by severity: critical > major > minor):
   - Reads the issue and its comments (fix suggestions from Phase 2)
   - Implements the fix
   - Commits with message referencing the issue: `fix: <description> (Fixes #N)`
   - If the fix touches an OpenCL kernel, also updates the corresponding Metal shader (and vice versa)
3. Pushes the branch to origin
4. Adds a comment on each implemented issue linking to the commit

**Model**: Opus — highest capability for implementation correctness, especially for GPU kernel code.

**Does NOT**:
- Merge the branch (user reviews first)
- Create a merge request (user decides when ready)
- Modify files outside the scope of the issue

## Skill File Structure

```
~/.claude/skills/review-codebase/
├── skill.md          # Skill definition and orchestration logic
```

The skill uses Claude Code's Agent tool to spawn each phase as a subagent with the appropriate model override.

## Error Handling

- If `GITLAB_TOKEN` is not set, abort with a clear message
- If GitLab API is unreachable, abort with connection error details
- If Phase 1 finds zero issues, skip Phases 2 and 3 and report "no issues found"
- If Phase 3 fails on a specific fix, skip it, comment on the issue that auto-fix failed, and continue to the next

## Labels

The skill will create these GitLab labels if they don't exist:
- `auto-review` (color: `#6699cc`)
- `bug` (color: `#d9534f`)
- `performance` (color: `#f0ad4e`)
- `severity::critical` (color: `#d9534f`)
- `severity::major` (color: `#f0ad4e`)
- `severity::minor` (color: `#5bc0de`)
