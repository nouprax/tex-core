#!/usr/bin/env bash
set -euo pipefail

: "${GH_REPO:?set GH_REPO=owner/repository}"
: "${RELEASE_REVIEWER:?set RELEASE_REVIEWER to a GitHub login}"

DEFAULT_BRANCH=${DEFAULT_BRANCH:-main}
RELEASE_ENVIRONMENT=${RELEASE_ENVIRONMENT:-release}
MAIN_RULESET_NAME=${MAIN_RULESET_NAME:-main quality gates}
OWNER_REVIEW_TEAM=${OWNER_REVIEW_TEAM:-}
OWNER_REVIEW_BYPASS_USER=${OWNER_REVIEW_BYPASS_USER:-$RELEASE_REVIEWER}
OWNER_REVIEW_RULESET_NAME=${OWNER_REVIEW_RULESET_NAME:-owner approval gate}
TAG_RULESET_NAME=${TAG_RULESET_NAME:-release tag protection}
TAG_PATTERN=${TAG_PATTERN:-v*.*.*}
RULESET_ENFORCEMENT=${RULESET_ENFORCEMENT:-evaluate}
PREVENT_SELF_REVIEW=${PREVENT_SELF_REVIEW:-false}

case "$RULESET_ENFORCEMENT" in
    disabled | evaluate | active) ;;
    *)
        echo "RULESET_ENFORCEMENT must be disabled, evaluate, or active" >&2
        exit 2
        ;;
esac

case "$PREVENT_SELF_REVIEW" in
    true | false) ;;
    *)
        echo "PREVENT_SELF_REVIEW must be true or false" >&2
        exit 2
        ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

reviewer_id=$(gh api "users/$RELEASE_REVIEWER" --jq .id)
repo_owner=${GH_REPO%%/*}
if [ "$repo_owner" = "$GH_REPO" ]; then
    echo "GH_REPO must use owner/repository form" >&2
    exit 2
fi

owner_review_team_id=
owner_review_bypass_id=
if [ -n "$OWNER_REVIEW_TEAM" ]; then
    owner_review_team_id=$(gh api "orgs/$repo_owner/teams/$OWNER_REVIEW_TEAM" --jq .id)
    owner_review_bypass_id=$(gh api "users/$OWNER_REVIEW_BYPASS_USER" --jq .id)
    # Required team approvals count only when the team has explicit Write access.
    gh api --method PUT \
        "orgs/$repo_owner/teams/$OWNER_REVIEW_TEAM/repos/$GH_REPO" \
        -f permission=push >/dev/null
fi

encoded_environment=$(jq -rn --arg value "$RELEASE_ENVIRONMENT" '$value | @uri')
actual_default=$(gh api "repos/$GH_REPO" --jq .default_branch)
if [ "$actual_default" != "$DEFAULT_BRANCH" ]; then
    echo "default branch is '$actual_default', expected '$DEFAULT_BRANCH'" >&2
    exit 1
fi

# Linear history: squash is the only merge method, and every squash commit
# is titled by the pull request ("title (#N)" + description body). The
# merge queue builds its commits from these same settings.
gh api --method PATCH "repos/$GH_REPO" \
    -F allow_squash_merge=true \
    -F allow_merge_commit=false \
    -F allow_rebase_merge=false \
    -f squash_merge_commit_title=PR_TITLE \
    -f squash_merge_commit_message=PR_BODY >/dev/null
echo "merge policy: squash-only, PR-title squash messages"

upsert_ruleset() {
    local name=$1
    local payload=$2
    local ids
    local count

    ids=$(gh api -H "X-GitHub-Api-Version: 2026-03-10" \
        "repos/$GH_REPO/rulesets" --paginate \
        --jq ".[] | select(.name == \"$name\") | .id")
    count=$(printf '%s\n' "$ids" | sed '/^$/d' | wc -l | tr -d ' ')

    if [ "$count" -gt 1 ]; then
        echo "multiple rulesets named '$name'; refusing ambiguous update" >&2
        exit 1
    elif [ "$count" -eq 1 ]; then
        gh api -H "X-GitHub-Api-Version: 2026-03-10" --method PUT \
            "repos/$GH_REPO/rulesets/$ids" --input "$payload" >/dev/null
    else
        gh api -H "X-GitHub-Api-Version: 2026-03-10" --method POST \
            "repos/$GH_REPO/rulesets" --input "$payload" >/dev/null
    fi
}

jq -n '{
  default_workflow_permissions: "read",
  can_approve_pull_request_reviews: false
}' >"$tmp/workflow-permissions.json"
gh api --method PUT "repos/$GH_REPO/actions/permissions/workflow" \
    --input "$tmp/workflow-permissions.json" >/dev/null

jq -n \
    --argjson reviewer_id "$reviewer_id" \
    --argjson prevent_self_review "$PREVENT_SELF_REVIEW" '{
    wait_timer: 0,
    prevent_self_review: $prevent_self_review,
    reviewers: [{type: "User", id: $reviewer_id}],
    deployment_branch_policy: {
      protected_branches: false,
      custom_branch_policies: true
    }
  }' >"$tmp/release-environment.json"
gh api --method PUT \
    "repos/$GH_REPO/environments/$encoded_environment" \
    --input "$tmp/release-environment.json" >/dev/null

matching_policies=$(gh api \
    "repos/$GH_REPO/environments/$encoded_environment/deployment-branch-policies" \
    --jq ".branch_policies[] | select(.name == \"$TAG_PATTERN\" and .type == \"tag\") | .id")
matching_count=$(printf '%s\n' "$matching_policies" | sed '/^$/d' | wc -l | tr -d ' ')
if [ "$matching_count" -eq 0 ]; then
    jq -n --arg name "$TAG_PATTERN" '{name: $name, type: "tag"}' \
        >"$tmp/release-tag-policy.json"
    gh api --method POST \
        "repos/$GH_REPO/environments/$encoded_environment/deployment-branch-policies" \
        --input "$tmp/release-tag-policy.json" >/dev/null
elif [ "$matching_count" -gt 1 ]; then
    echo "multiple identical release tag policies; clean them up before continuing" >&2
    exit 1
fi

policy_count=$(gh api \
    "repos/$GH_REPO/environments/$encoded_environment/deployment-branch-policies" \
    --jq '.branch_policies | length')
if [ "$policy_count" -ne 1 ]; then
    echo "release environment has $policy_count deployment policies; expected exactly one" >&2
    echo "remove unrelated branch/tag policies before activation" >&2
    exit 1
fi

jq -n \
    --arg name "$MAIN_RULESET_NAME" \
    --arg enforcement "$RULESET_ENFORCEMENT" '{
    name: $name,
    target: "branch",
    enforcement: $enforcement,
    conditions: {ref_name: {exclude: [], include: ["~DEFAULT_BRANCH"]}},
    rules: [
      {type: "deletion"},
      {type: "non_fast_forward"},
      {type: "pull_request", parameters: {
        require_code_owner_review: false,
        require_last_push_approval: false,
        dismiss_stale_reviews_on_push: false,
        required_approving_review_count: 0,
        required_review_thread_resolution: false
      }},
      {type: "required_status_checks", parameters: {
        do_not_enforce_on_create: true,
        strict_required_status_checks_policy: true,
        required_status_checks: [
          {context: "Required gates"},
          {context: "CodeQL gate"}
        ]
      }}
    ],
    bypass_actors: []
  }' >"$tmp/main-ruleset.json"
upsert_ruleset "$MAIN_RULESET_NAME" "$tmp/main-ruleset.json"

if [ -n "$OWNER_REVIEW_TEAM" ]; then
    jq -n \
        --arg name "$OWNER_REVIEW_RULESET_NAME" \
        --arg enforcement "$RULESET_ENFORCEMENT" \
        --argjson team_id "$owner_review_team_id" \
        --argjson owner_id "$owner_review_bypass_id" '{
      name: $name,
      target: "branch",
      enforcement: $enforcement,
      conditions: {ref_name: {exclude: [], include: ["~DEFAULT_BRANCH"]}},
      rules: [{type: "pull_request", parameters: {
        require_code_owner_review: false,
        require_last_push_approval: false,
        dismiss_stale_reviews_on_push: false,
        required_approving_review_count: 0,
        required_reviewers: [{
          file_patterns: ["*"],
          minimum_approvals: 1,
          reviewer: {id: $team_id, type: "Team"}
        }],
        required_review_thread_resolution: false
      }}],
      bypass_actors: [{
        actor_id: $owner_id,
        actor_type: "User",
        bypass_mode: "pull_request"
      }]
    }' >"$tmp/owner-review-ruleset.json"
    upsert_ruleset "$OWNER_REVIEW_RULESET_NAME" "$tmp/owner-review-ruleset.json"
fi

jq -n \
    --arg name "$TAG_RULESET_NAME" \
    --arg enforcement "$RULESET_ENFORCEMENT" \
    --arg pattern "refs/tags/$TAG_PATTERN" \
    --argjson reviewer_id "$reviewer_id" '{
    name: $name,
    target: "tag",
    enforcement: $enforcement,
    conditions: {ref_name: {exclude: [], include: [$pattern]}},
    rules: [
      {type: "creation"},
      {type: "update"},
      {type: "deletion"}
    ],
    bypass_actors: [{
      actor_id: $reviewer_id,
      actor_type: "User",
      bypass_mode: "always"
    }]
  }' >"$tmp/release-tag-ruleset.json"
upsert_ruleset "$TAG_RULESET_NAME" "$tmp/release-tag-ruleset.json"

echo "Repository control plane reconciled with enforcement=$RULESET_ENFORCEMENT"
