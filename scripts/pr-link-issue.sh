#!/usr/bin/env bash
# Link a pull request to an issue in the GitHub **Development** panel.
#
#   scripts/pr-link-issue.sh <pr-number> [issue-number] [--repo owner/name]
#
# With no issue number, the PR body is scanned for a link tag (see below) and
# that issue is used.
#
# WHY THIS SCRIPT EXISTS
#
# Every PR here must be linked to an issue, and `Closes #N` does not do it:
# GitHub only creates the relationship from a closing keyword when the PR
# targets the repository's DEFAULT branch.  This repo's default is `master`
# and virtually every PR targets `develop`, so keywords create no link at all.
#
# That used to mean the link could only be made by hand in the web UI, which
# is what .github/workflows/pr-issue-link.yml said.  That is no longer true:
# the GraphQL mutation `addCloseIssueReferences` -- "Adds one or more pull
# requests as manually linked closing references on an issue" -- is exactly
# the Development-panel link, and it is idempotent, so re-running is safe.
#
# LINK TAGS accepted in the PR body (case-insensitive):
#
#   Closes #123 / Fixes #123 / Resolves #123 / Refs #123
#   <!-- link-issue: #123 -->      (invisible in the rendered body)
#
# The keyword forms still do nothing on GitHub's side here; this script is
# what gives them effect.
set -euo pipefail

# Guard the file-mutating and network helpers against interactive shell
# aliases (`rm -i` and friends block forever with no TTY).
die() { printf 'pr-link-issue: %s\n' "$*" >&2; exit 1; }

PR=""; ISSUE=""; REPO=""
while [ $# -gt 0 ]; do
   case "$1" in
      --repo) REPO=${2:-}; shift 2 ;;
      -h|--help) sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
      *) if [ -z "$PR" ]; then PR=$1; elif [ -z "$ISSUE" ]; then ISSUE=$1;
         else die "unexpected argument '$1'"; fi; shift ;;
   esac
done

[ -n "$PR" ] || die "usage: pr-link-issue.sh <pr-number> [issue-number] [--repo owner/name]"
case "$PR" in ''|*[!0-9]*) die "PR must be a number, got '$PR'" ;; esac

if [ -z "$REPO" ]; then
   REPO=$(command gh repo view --json nameWithOwner --jq .nameWithOwner) \
      || die "not in a GitHub repo and --repo not given"
fi
OWNER=${REPO%%/*}; NAME=${REPO#*/}

# Already linked?  Nothing to do -- report it and stop, so this is safe to
# run unconditionally from CI or a hook.
existing=$(command gh pr view "$PR" --repo "$REPO" \
              --json closingIssuesReferences \
              --jq '[.closingIssuesReferences[].number] | join(",")')
if [ -n "$existing" ]; then
   echo "PR #$PR is already linked to: #${existing//,/, #}"
   exit 0
fi

# Strip fenced code blocks and inline code spans before scanning.  A PR that
# *documents* this feature quotes the tag syntax, and an unstripped scan
# happily links the example: PR #644 explained the tags with `#123` in a
# fenced block and got linked to issue #123 instead of the #643 tag at the
# bottom of the same body.  Anything inside code is a sample, not an
# instruction.
strip_code() {
   awk '
      /^[[:space:]]*```/ { fence = !fence; next }
      fence { next }
      { gsub(/`[^`]*`/, ""); print }
   '
}

# No issue given: scan the body for a link tag.  Hidden HTML-comment form
# wins over the keyword forms, so a PR can point at one issue in prose and
# link a different one deliberately.
if [ -z "$ISSUE" ]; then
   body=$(command gh pr view "$PR" --repo "$REPO" --json body --jq '.body // ""' \
          | strip_code)
   ISSUE=$(printf '%s' "$body" \
           | grep -oiE '<!--[[:space:]]*link-issue:[[:space:]]*#?[0-9]+[[:space:]]*-->' \
           | grep -oE '[0-9]+' | head -1 || true)
   if [ -z "$ISSUE" ]; then
      ISSUE=$(printf '%s' "$body" \
              | grep -oiE '(clos(e|es|ed)|fix(es|ed)?|resolv(e|es|ed)|refs?)[[:space:]]+#[0-9]+' \
              | grep -oE '[0-9]+' | head -1 || true)
   fi
   [ -n "$ISSUE" ] || die "PR #$PR body has no link tag (Closes/Fixes/Resolves/Refs #N, or <!-- link-issue: #N -->) and no issue number was given"
fi
case "$ISSUE" in ''|*[!0-9]*) die "issue must be a number, got '$ISSUE'" ;; esac

# The mutation refuses a PR as its target, but the error is opaque; check
# here so the message is useful.
kind=$(command gh api "repos/$REPO/issues/$ISSUE" \
          --jq 'if .pull_request then "pr" else "issue" end' 2>/dev/null) \
   || die "#$ISSUE does not exist in $REPO"
[ "$kind" = issue ] || die "#$ISSUE is a pull request, not an issue"

ids=$(command gh api graphql \
   -f query='query($o:String!,$n:String!,$p:Int!,$i:Int!){
      repository(owner:$o,name:$n){
        pullRequest(number:$p){ id }
        issue(number:$i){ id }
      }}' \
   -f o="$OWNER" -f n="$NAME" -F p="$PR" -F i="$ISSUE" \
   --jq '"\(.data.repository.pullRequest.id) \(.data.repository.issue.id)"')
pr_id=${ids%% *}; issue_id=${ids##* }

command gh api graphql \
   -f query='mutation($i:ID!,$p:[ID!]!){
      addCloseIssueReferences(input:{issueId:$i, pullRequestIds:$p}){
        clientMutationId
      }}' \
   -f i="$issue_id" -f p="$pr_id" >/dev/null

# Verify rather than trust the mutation's empty success payload.
now=$(command gh pr view "$PR" --repo "$REPO" \
         --json closingIssuesReferences \
         --jq '[.closingIssuesReferences[].number] | join(",")')
[ -n "$now" ] || die "mutation reported success but PR #$PR still has no linked issue"
echo "PR #$PR -> linked to #${now//,/, #}"
