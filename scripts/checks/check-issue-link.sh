#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# roam-39 (R9, §7.9): assert a consistent numeric N across a PR's head branch (…/roam-<N>-<slug>),
# title (ends "(roam-<N>)"), and body ("Closes #<N>"); and that #N is an OPEN ISSUE, not a PR.
# The consistency core runs offline. The open-issue lookup FAILS CLOSED and runs only with --repo
# (CI always passes --repo; local/structural use omits it). Title/body come via args from workflow env.
set -uo pipefail

BRANCH="" TITLE="" BODY="" REPO=""
while [ $# -gt 0 ]; do
  case "$1" in
    --branch) BRANCH="${2:-}"; shift 2;;
    --title)  TITLE="${2:-}";  shift 2;;
    --body)   BODY="${2:-}";   shift 2;;
    --repo)   REPO="${2:-}";   shift 2;;
    *) echo "issue-link: unknown arg: $1" >&2; exit 2;;
  esac
done

fail() { echo "issue-link: $1" >&2; exit 1; }

# Per-surface numeric extraction. Precise, boundary-anchored regex via python (portable across
# BSD/GNU grep, which disagree on \b): each pattern captures only the digits, in its own syntax, and
# rejects near-misses — branch requires a trailing "-<slug>"; title requires a terminal "(roam-N)";
# body requires a whole-word "Closes" and no trailing alnum after the number (so "Closes #12x" and
# "Encloses #12" are rejected).
extract() { printf '%s' "$2" | ROAMUX_RE="$1" python3 -c \
  'import os,re,sys; m=re.search(os.environ["ROAMUX_RE"], sys.stdin.read()); sys.stdout.write(m.group(1) if m else "")'; }
bn=$(extract '(?:^|/)roam-([0-9]+)-' "$BRANCH")
tn=$(extract '\(roam-([0-9]+)\)\s*$' "$TITLE")
bd=$(extract '(?i)(?:^|[^0-9A-Za-z])closes[ ]+#([0-9]+)(?![0-9A-Za-z])' "$BODY")

[ -n "$bn" ] || fail "branch does not contain '.../roam-<N>-<slug>': '$BRANCH'"
[ -n "$tn" ] || fail "title does not end with '(roam-<N>)': '$TITLE'"
[ -n "$bd" ] || fail "body does not contain 'Closes #<N>'"
{ [ "$bn" = "$tn" ] && [ "$tn" = "$bd" ]; } || fail "roam-N mismatch: branch=$bn title=$tn body=$bd"
N="$bn"

# Fail-closed open-issue-not-PR lookup.
if [ -n "$REPO" ]; then
  result=$(gh api "repos/$REPO/issues/$N" --jq 'if has("pull_request") then "pr" else .state end' 2>/dev/null) \
    || fail "gh api could not resolve #$N in $REPO (fail-closed: no network / not found / auth)"
  case "$result" in
    open) : ;;
    pr)   fail "#$N is a pull request, not an issue" ;;
    *)    fail "#$N is not an open issue (state=$result)" ;;
  esac
  echo "issue-link OK: roam-$N consistent across branch/title/body; #$N is an open issue."
else
  echo "issue-link OK (consistency only; --repo omitted): roam-$N across branch/title/body."
fi
