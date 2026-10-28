#!/usr/bin/env bash
# Creates a GitHub Project for issue tracking and links it to the repository.
# Requirements: GitHub CLI (gh) and jq, authenticated with the "project" scope:
#   gh auth refresh -s project
# Usage: OWNER=JorreFog REPO=BananaLinux ./scripts/setup-project.sh
set -euo pipefail

OWNER="${OWNER:-JorreFog}"
REPO="${REPO:-BananaLinux}"
TITLE="${TITLE:-BananaLinux Issue Tracker}"

command -v gh >/dev/null || { echo "gh (GitHub CLI) is required"; exit 1; }
command -v jq >/dev/null || { echo "jq is required"; exit 1; }

echo "Creating project '$TITLE' for $OWNER..."
project_json=$(gh project create --owner "$OWNER" --title "$TITLE" --format json)
number=$(jq -r '.number' <<< "$project_json")
url=$(jq -r '.url' <<< "$project_json")

echo "Linking project #$number to $OWNER/$REPO..."
gh project link "$number" --owner "$OWNER" --repo "$OWNER/$REPO"

# Replace the default Status options (Todo / In Progress / Done) with our columns.
status_field_id=$(gh project field-list "$number" --owner "$OWNER" --format json \
  | jq -r '.fields[] | select(.name == "Status") | .id')

echo "Configuring Status columns..."
gh api graphql -f fieldId="$status_field_id" -f query='
mutation($fieldId: ID!) {
  updateProjectV2Field(input: {
    fieldId: $fieldId,
    singleSelectOptions: [
      {name: "Triage",      color: GRAY,   description: "New items waiting for review"},
      {name: "Confirmed",   color: BLUE,   description: "Reproduced and accepted"},
      {name: "In progress", color: YELLOW, description: "Someone is working on it"},
      {name: "In review",   color: PURPLE, description: "Pull request open, waiting for review"},
      {name: "Done",        color: GREEN,  description: "Fixed or completed"}
    ]
  }) {
    projectV2Field {
      ... on ProjectV2SingleSelectField { id }
    }
  }
}' >/dev/null

echo
echo "Project created: $url"
echo "Next steps:"
echo "  1. In the project, add a Board view (View > New view > Board, group by Status)."
echo "  2. Project > Workflows: set 'Item added to project' -> Status: Triage."
echo "  3. Project > Workflows: set 'Item closed' and 'Pull request merged' -> Status: Done."
echo "  4. Set repo variable PROJECT_URL and secret PROJECT_TOKEN (see README.md)."
