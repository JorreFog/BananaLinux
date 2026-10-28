# Issue tracking

GitHub Issues is the single source of truth for bugs and feature requests.
This folder adds issue forms, a label set, a project board, a public dashboard
and a rule that only administrators set the priority.

## Files

| File | Purpose |
| --- | --- |
| `.github/ISSUE_TEMPLATE/bug_report.yml` | Bug form: build version, device, what the bug is, steps to reproduce. Adds `bug` and `needs-triage`. |
| `.github/ISSUE_TEMPLATE/feature_request.yml` | Feature form. Adds `enhancement` and `needs-triage`. |
| `.github/ISSUE_TEMPLATE/config.yml` | Disables blank issues. |
| `.github/workflows/enforce-priority.yml` | Removes priority labels set by non-administrators. |
| `.github/workflows/add-to-project.yml` | Adds new issues and pull requests to the project board. |
| `.github/workflows/dashboard.yml` | Builds and deploys the dashboard to GitHub Pages. |
| `scripts/setup-labels.sh` | Creates the label set. |
| `scripts/setup-project.sh` | Creates the project board and its columns. |
| `scripts/generate-dashboard-data.mjs` | Fetches issues and writes `site/data.json`. |
| `site/index.html` | The dashboard. |

## Who does what

- **Reporter:** fills in the form. Cannot set labels.
- **Triagers (Triage role or higher):** set `confirmed` or `needs-info`, and the type and area labels. They remove `needs-triage`.
- **Administrators:** are the only ones who set the priority (`P0-critical` to `P3-low`).
  If anyone else adds a priority label, the workflow removes it and leaves a comment.

## Labels

- **Type:** `bug`, `enhancement`, `question`
- **Status:** `needs-triage`, `needs-info`, `confirmed`, `wontfix`, `duplicate`
- **Priority (admins only):** `P0-critical`, `P1-high`, `P2-normal`, `P3-low`
- **Area:** `installer`, `kernel`, `desktop`, `packages`, `build-system`

## Board columns

`Triage` -> `Confirmed` -> `In progress` -> `In review` -> `Done`

## Setup (after this is merged into the default branch)

1. Authenticate the GitHub CLI and run the scripts as a repository administrator:
   ```bash
   gh auth login
   gh auth refresh -s project
   ./scripts/setup-labels.sh
   ./scripts/setup-project.sh
   ```
2. Board automation:
   - Create a personal access token with the `project` scope.
   - Add it as the repository secret `PROJECT_TOKEN`.
   - Add the repository variable `PROJECT_URL` (example: `https://github.com/users/JorreFog/projects/1`).
   - In the project, open **Workflows**: item added -> `Triage`, item closed -> `Done`, pull request merged -> `Done`.
   - Add a **Board** view grouped by Status.
3. Dashboard:
   - Go to **Settings > Pages** and set **Source** to **GitHub Actions**.
   - Open **Actions > Deploy dashboard** and click **Run workflow**.
   - The page will be at `https://<owner>.github.io/<repo>/`.

## Dashboard notes

- It refreshes every hour and whenever an issue changes.
- The build version comes from the "BananaLinux version" field of the bug form. Issues created without the form show as "Unknown".
- To preview locally:
  ```bash
  GITHUB_TOKEN=<token> GITHUB_REPOSITORY=<owner>/<repo> node scripts/generate-dashboard-data.mjs
  npx serve site
  ```
  Add `site/data.json` to `.gitignore`; it is generated on every deploy.
- GitHub pauses scheduled workflows after 60 days without repository activity. Issue events and manual runs still work.
