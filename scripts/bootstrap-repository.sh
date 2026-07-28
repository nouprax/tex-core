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
# Mature-repository defaults: full enforcement, no standing bypass. A
# re-run with only the required inputs must never weaken protections that
# are already active; pass ALLOW_PROTECTION_DOWNGRADE=true explicitly to
# downgrade a live ruleset (initial bootstrap of a fresh repository can
# still opt into RULESET_ENFORCEMENT=evaluate while CI stabilizes).
RULESET_ENFORCEMENT=${RULESET_ENFORCEMENT:-active}
PREVENT_SELF_REVIEW=${PREVENT_SELF_REVIEW:-false}
MERGE_QUEUE=${MERGE_QUEUE:-true}
MAIN_ADMIN_BYPASS=${MAIN_ADMIN_BYPASS:-false}
ALLOW_PROTECTION_DOWNGRADE=${ALLOW_PROTECTION_DOWNGRADE:-false}

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

for flag in MERGE_QUEUE MAIN_ADMIN_BYPASS; do
    case "${!flag}" in
        true | false) ;;
        *)
            echo "$flag must be true or false" >&2
            exit 2
            ;;
    esac
done

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

# Linear history: squash is the only merge method. The title/message
# settings are repository defaults — a direct merge can still edit them
# before confirming — while merge-queue commits apply them mechanically
# ("title (#N)" + description body).
gh api --method PATCH "repos/$GH_REPO" \
    -F allow_squash_merge=true \
    -F allow_merge_commit=false \
    -F allow_rebase_merge=false \
    -f squash_merge_commit_title=PR_TITLE \
    -f squash_merge_commit_message=PR_BODY >/dev/null
echo "merge policy: squash-only; squash messages default to the PR title"

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
        # Fail closed on protection downgrades: a maintenance re-run must
        # not weaken a live ruleset (active -> evaluate/disabled) or add a
        # bypass where none exists, unless the operator says so explicitly.
        if [ "$ALLOW_PROTECTION_DOWNGRADE" != "true" ]; then
            local current_enforcement current_bypasses requested_enforcement requested_bypasses
            current_enforcement=$(gh api -H "X-GitHub-Api-Version: 2026-03-10" \
                "repos/$GH_REPO/rulesets/$ids" --jq .enforcement)
            current_bypasses=$(gh api -H "X-GitHub-Api-Version: 2026-03-10" \
                "repos/$GH_REPO/rulesets/$ids" --jq '.bypass_actors | length')
            requested_enforcement=$(jq -r .enforcement "$payload")
            requested_bypasses=$(jq -r '.bypass_actors | length' "$payload")
            if [ "$current_enforcement" = "active" ] && [ "$requested_enforcement" != "active" ]; then
                echo "refusing to downgrade '$name' from active to $requested_enforcement;" >&2
                echo "set ALLOW_PROTECTION_DOWNGRADE=true to override deliberately" >&2
                exit 1
            fi
            if [ "$requested_bypasses" -gt "$current_bypasses" ]; then
                echo "refusing to add bypass actors to '$name' ($current_bypasses -> $requested_bypasses);" >&2
                echo "set ALLOW_PROTECTION_DOWNGRADE=true to override deliberately" >&2
                exit 1
            fi
        fi
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

# The merge queue keeps every PR's required checks green against the
# latest default branch and squashes with the repository's message
# defaults. The admin bypass mirrors the operator's standing exception;
# disable either through its variable for the hardened Phase-10 shape.
jq -n \
    --arg name "$MAIN_RULESET_NAME" \
    --arg enforcement "$RULESET_ENFORCEMENT" \
    --argjson queue "$MERGE_QUEUE" \
    --argjson admin_bypass "$MAIN_ADMIN_BYPASS" '{
    name: $name,
    target: "branch",
    enforcement: $enforcement,
    conditions: {ref_name: {exclude: [], include: ["~DEFAULT_BRANCH"]}},
    rules: ([
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
    ] + (if $queue then [
      {type: "merge_queue", parameters: {
        merge_method: "SQUASH",
        grouping_strategy: "ALLGREEN",
        max_entries_to_build: 5,
        min_entries_to_merge: 1,
        max_entries_to_merge: 5,
        min_entries_to_merge_wait_minutes: 5,
        check_response_timeout_minutes: 60
      }}
    ] else [] end)),
    bypass_actors: (if $admin_bypass then [
      {actor_id: 5, actor_type: "RepositoryRole", bypass_mode: "always"}
    ] else [] end)
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
