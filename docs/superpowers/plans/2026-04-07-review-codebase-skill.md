# Review Codebase Skill — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a `/review-codebase` Claude Code skill that audits the full codebase for bugs and performance issues using three tiered agents, tracks findings as GitLab issues, and implements fixes on a review branch.

**Architecture:** A single `skill.md` file orchestrates three sequential phases using Claude Code's Agent tool with model overrides. Each phase communicates via GitLab issues (created by Phase 1, enriched by Phase 2, implemented by Phase 3). GitLab API interaction uses `curl` via Bash tool.

**Tech Stack:** Claude Code skills (markdown), GitLab REST API v4, `curl`, `jq`, `git`

---

### Task 1: Create GitLab Labels

**Files:**
- Create: `~/.claude/skills/review-codebase/skill.md`

This task creates the skill file with the label-creation preamble. Subsequent tasks will append to it.

- [ ] **Step 1: Create skill directory**

Run:
```bash
mkdir -p ~/.claude/skills/review-codebase
```

- [ ] **Step 2: Write the skill file with frontmatter and label setup**

Create `~/.claude/skills/review-codebase/skill.md` with the following content:

````markdown
---
name: review-codebase
description: Full codebase audit for bugs and performance issues. Spawns three agents (Haiku reviewer, Sonnet commenter, Opus implementer) that create GitLab issues, triage them, and implement fixes on a review branch.
user_invocable: true
---

# /review-codebase

Automated full-codebase review for performance enhancements and bug fixes.

## Prerequisites

- `GITLAB_TOKEN` environment variable must be set with a GitLab personal access token (api scope)
- GitLab instance at `https://git.bandrel.com` must be reachable

## Configuration

```
GITLAB_API=https://git.bandrel.com/api/v4
GITLAB_PROJECT_ID=5
```

## Execution

When this skill is invoked, follow these steps exactly in order.

### Step 0: Validate environment

Run this command using the Bash tool:

```bash
if [ -z "$GITLAB_TOKEN" ]; then echo "ERROR: GITLAB_TOKEN not set. Export it in ~/.zshrc and restart Claude Code."; exit 1; fi && curl -sf --connect-timeout 5 --header "PRIVATE-TOKEN: $GITLAB_TOKEN" "https://git.bandrel.com/api/v4/user" | python3 -c "import sys,json; u=json.load(sys.stdin); print(f'Authenticated as {u[\"username\"]} ({u[\"name\"]})')" 2>&1 || echo "ERROR: GitLab API unreachable or token invalid"
```

If the output starts with "ERROR", stop and tell the user what went wrong. Do not proceed.

### Step 1: Ensure GitLab labels exist

Run this command using the Bash tool. It creates labels if they don't already exist (409 = already exists, which is fine):

```bash
GITLAB_API="https://git.bandrel.com/api/v4"
PROJECT_ID=5
for label_json in \
  '{"name":"auto-review","color":"#6699cc"}' \
  '{"name":"bug","color":"#d9534f"}' \
  '{"name":"performance","color":"#f0ad4e"}' \
  '{"name":"severity::critical","color":"#d9534f"}' \
  '{"name":"severity::major","color":"#f0ad4e"}' \
  '{"name":"severity::minor","color":"#5bc0de"}'; do
  curl -sf -o /dev/null -w "%{http_code} " \
    --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data "$label_json" \
    "$GITLAB_API/projects/$PROJECT_ID/labels" 2>/dev/null
done
echo "Labels ensured."
```

### Step 2: Phase 1 — Reviewer (Haiku)

Use the Agent tool to spawn a subagent with `model: "haiku"` and `subagent_type: "general-purpose"`.

Give the agent this prompt:

---

**PROMPT FOR PHASE 1 AGENT:**

You are a code reviewer for the rainbowcrackalack project — a GPU-accelerated rainbow table generator in C with OpenCL and Metal backends.

**Your task:** Review the ENTIRE codebase for bugs and performance issues. Read every source file.

**Scope — read ALL of these files:**
- All `.c` and `.h` files in the project root
- All `.cl` files in `CL/`
- All `.metal` files in `Metal/`

**What to look for:**

Bugs:
- Off-by-one errors, out-of-bounds array access
- Race conditions in GPU kernels or pthreaded host code
- Memory leaks (malloc without free, GPU buffer leaks)
- Null pointer dereference
- Integer overflow (especially in index calculations with unsigned long)
- Uninitialized variables
- Incorrect pointer arithmetic

Performance:
- Non-coalesced global memory reads/writes in GPU kernels
- Shared/local memory bank conflicts
- Low GPU occupancy hints (excessive register usage, large local arrays)
- Redundant computation inside loops
- Unnecessary global memory traffic (values that could be cached in registers or local memory)
- Missed local/shared memory caching opportunities
- Inefficient loop structures or branching patterns

**Output format:** For each finding, output a JSON object on its own line (JSONL format). Output ONLY the JSON lines, no other text:

```
{"file":"path/to/file.c","line_start":42,"line_end":45,"category":"bug","severity":"major","title":"Short imperative title","description":"Detailed explanation of the issue and why it matters."}
```

Valid values: category = "bug" or "performance". severity = "critical", "major", or "minor".

Be thorough but avoid false positives. Only report issues you are confident about. Do not report style issues, missing comments, or naming conventions. Focus on correctness and performance.

After reading all files and collecting findings, output ALL findings as JSONL.

---

After the agent returns, parse its output to extract the JSONL findings. For each finding, create a GitLab issue using the Bash tool:

```bash
GITLAB_API="https://git.bandrel.com/api/v4"
PROJECT_ID=5

# For each finding, run:
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  --header "Content-Type: application/json" \
  --data '{
    "title": "[auto-review] TITLE_HERE",
    "description": "**File:** `FILE_HERE`\n**Lines:** LINE_START-LINE_END\n**Category:** CATEGORY\n**Severity:** SEVERITY\n\n---\n\nDESCRIPTION_HERE",
    "labels": "auto-review,CATEGORY,severity::SEVERITY"
  }' \
  "$GITLAB_API/projects/$PROJECT_ID/issues"
```

Replace TITLE_HERE, FILE_HERE, LINE_START, LINE_END, CATEGORY, SEVERITY, and DESCRIPTION_HERE with values from the finding JSON.

Print a summary: "Phase 1 complete: N issues created."

If zero findings, print "Phase 1 complete: no issues found. Codebase looks clean." and STOP — do not proceed to Phase 2 or 3.

### Step 3: Phase 2 — Commenter (Sonnet)

First, fetch all open issues with the `auto-review` label:

```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  "https://git.bandrel.com/api/v4/projects/5/issues?labels=auto-review&state=opened&per_page=100" \
  | python3 -c "
import sys, json
issues = json.load(sys.stdin)
for i in issues:
    print(json.dumps({'iid': i['iid'], 'title': i['title'], 'description': i['description'], 'labels': i['labels']}))
"
```

For each issue, use the Agent tool to spawn a subagent with `model: "sonnet"` and `subagent_type: "general-purpose"`.

Give each agent this prompt (fill in ISSUE_IID, ISSUE_TITLE, ISSUE_DESCRIPTION with the actual values):

---

**PROMPT FOR PHASE 2 AGENT:**

You are a senior code reviewer for the rainbowcrackalack project — a GPU-accelerated rainbow table generator in C with OpenCL and Metal backends.

**Context:** An automated reviewer found a potential issue. Your job is to determine if it's valid and, if so, provide actionable fix guidance.

**Issue #ISSUE_IID:** ISSUE_TITLE

ISSUE_DESCRIPTION

**Your task:**
1. Read the referenced file and surrounding context (at least 50 lines before and after)
2. Determine if this is a real issue or a false positive
3. Output your assessment as JSON:

If **false positive:**
```json
{"verdict": "false_positive", "explanation": "Why this is not actually a problem."}
```

If **valid issue:**
```json
{"verdict": "valid", "root_cause": "What's actually causing the issue.", "fix_approach": "Specific steps to fix it, referencing exact lines and code changes.", "impact": "Expected improvement or risk if not fixed.", "risks": "Side effects or things to watch out for.", "related_files": ["other/files/that/may/need/updating.c"]}
```

Output ONLY the JSON, no other text. Read the actual source code before making your determination.

---

After each agent returns, parse the JSON response:

- If `verdict` is `"false_positive"`: close the issue with a comment:
  ```bash
  # Add comment
  curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data '{"body": "**Auto-triage: False positive**\n\nEXPLANATION_HERE"}' \
    "https://git.bandrel.com/api/v4/projects/5/issues/ISSUE_IID/notes"

  # Close the issue
  curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data '{"state_event": "close"}' \
    "https://git.bandrel.com/api/v4/projects/5/issues/ISSUE_IID"
  ```

- If `verdict` is `"valid"`: add a comment with the analysis:
  ```bash
  curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data '{"body": "**Auto-triage: Valid issue**\n\n**Root cause:** ROOT_CAUSE\n\n**Suggested fix:** FIX_APPROACH\n\n**Expected impact:** IMPACT\n\n**Risks:** RISKS\n\n**Related files:** RELATED_FILES"}' \
    "https://git.bandrel.com/api/v4/projects/5/issues/ISSUE_IID/notes"
  ```

Print a summary: "Phase 2 complete: N valid, M false positives closed."

If all issues were closed as false positives, print that and STOP — do not proceed to Phase 3.

### Step 4: Phase 3 — Implementer (Opus)

First, create the review branch and fetch remaining open issues:

```bash
git checkout -b auto-review-$(date +%Y-%m-%d) master
```

```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  "https://git.bandrel.com/api/v4/projects/5/issues?labels=auto-review&state=opened&per_page=100" \
  | python3 -c "
import sys, json
issues = json.load(sys.stdin)
# Sort by severity: critical first, then major, then minor
severity_order = {'severity::critical': 0, 'severity::major': 1, 'severity::minor': 2}
def sort_key(i):
    for l in i['labels']:
        if l in severity_order:
            return severity_order[l]
    return 3
issues.sort(key=sort_key)
for i in issues:
    print(json.dumps({'iid': i['iid'], 'title': i['title'], 'description': i['description'], 'labels': i['labels']}))
"
```

For each issue, also fetch its comments (notes) to get the Phase 2 fix suggestions:

```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  "https://git.bandrel.com/api/v4/projects/5/issues/ISSUE_IID/notes" \
  | python3 -c "
import sys, json
notes = json.load(sys.stdin)
for n in notes:
    if 'Auto-triage' in n.get('body', ''):
        print(n['body'])
"
```

For each issue, use the Agent tool to spawn a subagent with `model: "opus"` and `subagent_type: "general-purpose"`.

Give each agent this prompt (fill in ISSUE_IID, ISSUE_TITLE, ISSUE_DESCRIPTION, FIX_SUGGESTION with actual values):

---

**PROMPT FOR PHASE 3 AGENT:**

You are implementing a fix for the rainbowcrackalack project — a GPU-accelerated rainbow table generator in C with OpenCL and Metal backends.

**Issue #ISSUE_IID:** ISSUE_TITLE

ISSUE_DESCRIPTION

**Suggested fix from code review:**

FIX_SUGGESTION

**Your task:**
1. Read the referenced file(s) and understand the issue
2. Implement the fix using the Edit tool
3. If you modify an OpenCL kernel in `CL/`, also update the corresponding Metal shader in `Metal/` (and vice versa). The Metal versions are mechanical translations of OpenCL — keep them in sync. Key Metal differences: `unsigned long` is 32-bit (use `ulong` for 64-bit), no `long long`, inline functions need explicit address space qualifiers (`thread`/`device`/`constant`).
4. Do NOT modify files unrelated to this issue
5. Do NOT add tests (the project uses GPU-accelerated unit tests that require hardware)
6. After making changes, stage and commit:

```bash
git add <changed files>
git commit -m "fix: <short description> (Fixes #ISSUE_IID)"
```

Make the minimal change needed to fix the issue. Do not refactor surrounding code.

---

After each agent completes, verify the commit was created:

```bash
git log --oneline -1
```

After all issues are implemented, push the branch and comment on each issue:

```bash
git push -u origin auto-review-$(date +%Y-%m-%d)
```

For each implemented issue, add a comment with the commit hash:

```bash
COMMIT_HASH=$(git log --all --oneline --grep="Fixes #ISSUE_IID" | head -1 | cut -d' ' -f1)
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  --header "Content-Type: application/json" \
  --data "{\"body\": \"Fix implemented in commit $COMMIT_HASH on branch auto-review-$(date +%Y-%m-%d). Pending review.\"}" \
  "https://git.bandrel.com/api/v4/projects/5/issues/ISSUE_IID/notes"
```

Print final summary: "Phase 3 complete: N fixes implemented on branch auto-review-YYYY-MM-DD. Review and merge when ready."

### Step 5: Switch back to master

After Phase 3 completes (or if skipped), switch back to master:

```bash
git checkout master
```
````

- [ ] **Step 3: Verify the skill is detected**

Run:
```bash
ls -la ~/.claude/skills/review-codebase/skill.md
```
Expected: file exists with the content from Step 2.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-04-07-review-codebase-skill.md
git commit -m "docs: implementation plan for review-codebase skill"
```

Note: The skill file lives in `~/.claude/skills/` (outside the repo), so it isn't committed to the project.

---

### Task 2: Test Label Creation

- [ ] **Step 1: Test the label creation command**

Run:
```bash
GITLAB_API="https://git.bandrel.com/api/v4"
PROJECT_ID=5
for label_json in \
  '{"name":"auto-review","color":"#6699cc"}' \
  '{"name":"bug","color":"#d9534f"}' \
  '{"name":"performance","color":"#f0ad4e"}' \
  '{"name":"severity::critical","color":"#d9534f"}' \
  '{"name":"severity::major","color":"#f0ad4e"}' \
  '{"name":"severity::minor","color":"#5bc0de"}'; do
  curl -sf -o /dev/null -w "%{http_code} " \
    --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data "$label_json" \
    "$GITLAB_API/projects/$PROJECT_ID/labels" 2>/dev/null
done
echo "Labels ensured."
```

Expected: Mix of `201` (created) and `409` (already exists). All labels should appear in GitLab under the project's labels page.

- [ ] **Step 2: Verify labels in GitLab**

Run:
```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  "https://git.bandrel.com/api/v4/projects/5/labels?per_page=100" \
  | python3 -c "import sys,json; [print(f'{l[\"name\"]:25s} {l[\"color\"]}') for l in json.load(sys.stdin)]"
```

Expected: All 6 labels listed with correct colors.

---

### Task 3: Test Issue Creation and Closure

- [ ] **Step 1: Create a test issue**

Run:
```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  --header "Content-Type: application/json" \
  --data '{
    "title": "[auto-review] TEST: sample finding",
    "description": "**File:** `test.c`\n**Lines:** 1-5\n**Category:** bug\n**Severity:** minor\n\n---\n\nThis is a test issue to verify the pipeline works.",
    "labels": "auto-review,bug,severity::minor"
  }' \
  "https://git.bandrel.com/api/v4/projects/5/issues" \
  | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'Created issue #{d[\"iid\"]}')"
```

Expected: `Created issue #N`

- [ ] **Step 2: Add a comment to the test issue**

Run (replace IID with the issue number from Step 1):
```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  --header "Content-Type: application/json" \
  --data '{"body": "**Auto-triage: False positive**\n\nThis is a test comment."}' \
  "https://git.bandrel.com/api/v4/projects/5/issues/IID/notes" \
  | python3 -c "import sys,json; print('Comment added')"
```

Expected: `Comment added`

- [ ] **Step 3: Close the test issue**

Run (replace IID):
```bash
curl -sf --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
  --header "Content-Type: application/json" \
  --data '{"state_event": "close"}' \
  "https://git.bandrel.com/api/v4/projects/5/issues/IID" \
  | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'Issue #{d[\"iid\"]} state: {d[\"state\"]}')"
```

Expected: `Issue #N state: closed`

- [ ] **Step 4: Commit**

No code changes in this task — this was API validation only.

---

### Task 4: Write the Skill File

- [ ] **Step 1: Create the skill directory and file**

Run:
```bash
mkdir -p ~/.claude/skills/review-codebase
```

Then create `~/.claude/skills/review-codebase/skill.md` with the full content from Task 1, Step 2 (the complete skill.md content within the code fence).

- [ ] **Step 2: Verify skill file**

Run:
```bash
head -5 ~/.claude/skills/review-codebase/skill.md
```

Expected:
```
---
name: review-codebase
description: Full codebase audit for bugs and performance issues. Spawns three agents (Haiku reviewer, Sonnet commenter, Opus implementer) that create GitLab issues, triage them, and implement fixes on a review branch.
user_invocable: true
---
```

- [ ] **Step 3: Commit the plan**

```bash
cd /Users/justinbollinger/projects/rainbowcrackalack
git add docs/superpowers/plans/2026-04-07-review-codebase-skill.md
git commit -m "docs: implementation plan for review-codebase skill"
```

---

### Task 5: End-to-End Smoke Test

- [ ] **Step 1: Invoke the skill**

In Claude Code, type `/review-codebase` and let it run through all three phases.

- [ ] **Step 2: Verify results**

Check GitLab for:
- Issues created with `auto-review` label
- Comments on valid issues with fix suggestions
- Some issues closed as false positives
- A branch `auto-review-YYYY-MM-DD` pushed with fix commits

- [ ] **Step 3: Review the branch**

```bash
git log master..auto-review-$(date +%Y-%m-%d) --oneline
```

Review each commit. Merge or cherry-pick as desired.
