---
name: sec-approval-draft
description: Draft a Bugzilla sec-approval request for a Firefox security bug. Reads the investigation file and patch details to fill in all fields. Triggers on: "sec approval", "write sec approval", "sec-approval", "/sec-approval".
argument-hint: <bug-id>
allowed-tools: [Read, Bash, WebFetch]
---

# Sec-Approval Request Writer

Draft a Bugzilla sec-approval comment for a Firefox security bug, using the
investigation file and patch details to fill every field accurately.

## Steps

### 1. Gather context

**Read the investigation file** (if it exists):
```
~/firefox-bug-investigation/bug-{bug_id}-investigation.md
```

**Fetch the bug** — security bugs require `bmo-to-md` (reads `$BUGZILLA_API_KEY`):
```bash
bmo-to-md {bug_id}
```
Extract: keywords, component, sec-* level, PoC availability, exploit difficulty
from comments, and any existing branch/version info.

**Check the current Firefox version**:
```bash
cat ~/firefox/config/milestone.txt
```

**Check currently supported release branches** — fetch the release calendar
to determine which versions are on Nightly, Beta, Release, and ESR:
```
WebFetch: https://whattrainisitnow.com/calendar/
```
Extract the current Nightly, Beta, Release, and ESR version numbers.

**Find what introduced the flaw** — check git log for the commit that first
introduced the buggy code pattern:
```bash
git -C ~/firefox log --oneline --follow -S "<key_symbol>" -- <path/to/file> | head -10
```
Use the bug number from the earliest relevant commit. Then cross-reference
with the release calendar to determine which supported branches carry the
vulnerable code.

### 2. Draft the request

Use this exact format — every field is required. Do NOT paste the draft into
chat: sec-approval answers are sensitive and belong in the bug's investigation
doc. Write them (markdown: bold each question, answer on the next line) into
`~/firefox-bug-investigation/bug-{bug_id}-investigation.md` under the
`## Security Approval` section — see step 4.

```
**How easily could an exploit be constructed based on the patch?**
{answer}

**Do comments in the patch, the check-in comment, or tests included in the patch paint a bulls-eye on the security problem?**
{Yes/No — explain if Yes}

**Which branches (beta, release, and/or ESR) are affected by this flaw, and do the release status flags reflect this affected/unaffected state correctly?**
{answer}

**If not all supported branches, which bug introduced the flaw?**
{Bug XXXXXXX}

**Do you have backports for the affected branches?**
{Yes/No}

**If not, how different, hard to create, and risky will they be?**
{answer or N/A}

**How likely is this patch to cause regressions; how much testing does it need?**
{answer}

**Is the patch ready to land after security approval is given?**
{Yes/No}

**Is Android affected?**
{Yes/No/Unknown}
```

When posting to Bugzilla, strip the markdown bold markers (`**`) since
Bugzilla comments are plain text.

### Field guidance

Keep all answers short and factual. The audience is the sec team — omit
internal implementation details, code references, and stack traces. State
conclusions, not reasoning.

**Evaluate the defect independently — do not inherit the bug's existing sec-*
rating or `csectype-*` keywords.** Base the exploit-difficulty and branch/impact
answers on your own reading of the patch and the actual defect (what an attacker
can achieve, read vs. write, which process / trust boundary is crossed, what
capability is gained, and whether it is exploitable alone or only when chained),
not on the rating already set on the bug. If your assessment disagrees with the
existing rating/keywords, note the discrepancy for the user rather than silently
deferring to the bug — the draft should reflect the true severity.

**How easily could an exploit be constructed?**
- Always open with one of: "Easy.", "Moderate.", or "Difficult."
- 2-3 sentences max.
- Base the answer on the patch itself — what the fix reveals about the
  vulnerable code path — and on whether an attacker could recreate the
  conditions using only publicly available web APIs. Do not reference the
  testcase (it is security-restricted and not visible to others).
- Note what privileges/conditions are required and whether reliable
  exploitation needs additional work (timing, heap shaping, etc.).
  Do not mention sandbox escape — the sec team knows the content process
  sandbox applies. No code or technical internals.
- Use this rubric to choose the difficulty level:
  - Easy: trigger is obvious from the patch and reproducible with standard
    web APIs alone; no special timing or heap work needed.
  - Moderate: trigger is discoverable from the patch + public API knowledge,
    but reliable exploitation needs one additional factor (timing, heap
    shaping, or a non-default condition).
  - Difficult: trigger is non-obvious from the patch and requires independent
    discovery; OR reliable exploitation needs two or more independent factors
    (e.g. non-obvious event vector + deterministic GC timing + heap shaping).
    For race conditions, also use Difficult when the patch only fixes one side
    of the race — the other racing operation and the API sequence needed to
    reproduce the window are not disclosed by the patch.

**Bulls-eye?**
- Check the commit message, patch comments, and test names/comments.
- Answer No if the fix description is neutral and mechanical.

**Branches affected?**
- **MUST lead with the concrete version number of every named channel** —
  Nightly, Beta, Release, and each active ESR line — each marked affected or
  unaffected. Resolve the numbers first: `config/milestone.txt` gives Nightly
  (`Beta = Nightly-1`, `Release = Nightly-2`), and the release calendar
  (`https://whattrainisitnow.com/calendar/`) gives the exact Release/ESR lines
  (there are often two ESR lines active at once). Never write "current
  branches", "all supported branches", "recent branches", or "confirm which
  branches" in place of a number.
- **NO vague/deferred prose.** If a pref/feature gate decides reachability,
  resolve it now (read the default per channel in `StaticPrefList.yaml` — watch
  `#ifdef NIGHTLY_BUILD`, `@IS_NOT_ANDROID@`, `XP_WIN`, etc.) and state the
  resulting per-version affected/unaffected split. Do not defer with
  "[Assumption]" or "to be confirmed" — a branches answer that names no versions
  is not acceptable in the final draft.
- Include the Firefox version in which the flaw was introduced (from the git
  log result), so the sec team can tell at a glance which branches need patching.
- State the correct release-status flags explicitly (e.g. `status-firefox155`).
- Example format: "Introduced in Firefox 118 (Bug XXXXXXX). Affects Nightly
  (155), Beta (154), Release (153), ESR 140, and ESR 153. Set status-firefox155
  affected; 154/153/ESR-140/ESR-153 unaffected." Keep it this terse — one or two
  lines of version-tagged facts, not a paragraph.

**Which bug introduced the flaw?**
- Use the git log result from Step 1.
- If introduced at initial implementation, name the bug that added the feature.

**Backports?**
- Yes if ready. No if not.
- If No: one sentence on self-containedness and risk. No code details.

**Regression risk / testing?**
- Low/moderate/high, one sentence of justification.

**Ready to land?**
- Yes if patches are on Phabricator, reviewed, and CI is clean.
- No if still waiting on review or try runs.

**Android affected?**
- Yes if the affected code is cross-platform (e.g. DOM, media pipeline).
- No if desktop-only (e.g. Windows-specific, GTK).
- Unknown if untested.

### 3. Self-check before presenting

**⚠️ MANDATORY: Scan every answer for forbidden content and remove it.**

The investigation file you read in step 1 is full of technical detail that
must NOT bleed into this draft. Go through each answer and remove:

- [ ] Function names (e.g. `ParseStRefPicSet`, `ReadBit`)
- [ ] Variable or type names (e.g. `numDeltaPocs`, `bool[64]`, `uint32_t`)
- [ ] File paths or line numbers (e.g. `H265.cpp:651`)
- [ ] Code snippets or pseudo-code
- [ ] Stack traces or ASAN output
- [ ] Internal reasoning ("because X calls Y which writes to Z")

Each answer must state a **conclusion only**. If you catch yourself explaining
*how* the bug works mechanically, delete it and replace with the conclusion.

**Also verify (required content, not just forbidden content):**
- [ ] The **Branches affected** answer names an explicit **version number** for
  every channel (Nightly / Beta / Release / each ESR) with an affected/unaffected
  verdict, and contains **no** vague placeholder ("current/all/recent branches",
  "confirm which branches", "[Assumption]"). If a pref gates reachability, its
  per-channel default was resolved and reflected in the version split.

**IF ANY FORBIDDEN CONTENT REMAINS — OR THE BRANCHES ANSWER LACKS VERSION
NUMBERS — FIX IT BEFORE PROCEEDING.**

### 4. Write to the investigation doc, then point the user to it

Write the completed draft into
`~/firefox-bug-investigation/bug-{bug_id}-investigation.md`, filling the
`## Security Approval` section (the `/bug-start` template creates it; if the
section is missing, add it immediately after `## Security Rating`). Replace any
placeholder fields with the real answers — one bolded question + answer per the
step-2 format.

Do **not** paste the draft into chat — it is sensitive and the doc is its home
(local, private). In chat report only: a one-line confirmation with the file
path + `## Security Approval` section, the chosen difficulty rating, and any
rating/`csectype` discrepancy you noted. Then ask whether to post it to
Bugzilla.

Only when the user explicitly approves posting: read the answers back from the
doc, strip the markdown bold markers (`**`), and post as a plain-text Bugzilla
comment.
