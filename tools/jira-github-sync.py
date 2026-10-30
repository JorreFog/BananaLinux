#!/usr/bin/env python3
"""Sync open GitHub issues and pull requests into the BananaLinux Jira board.

Creates missing Jira issues with auto-assigned priority, labels them for
idempotent re-runs, links back to GitHub, and moves them onto the board:
  issues → Github Issues (Bugs / Feature requests epics)
  PRs    → In Review, or In Progress if draft (Pull requests epic)
  merged/closed PRs already on the board → Done

Required env:
  JIRA_BASE_URL   e.g. https://bananalinux.atlassian.net
  JIRA_EMAIL      Atlassian account email
  JIRA_API_TOKEN  https://id.atlassian.com/manage-profile/security/api-tokens
  GITHUB_TOKEN    optional; uses gh auth / Actions token when unset

Optional env:
  JIRA_PROJECT_KEY   default KAN
  GITHUB_REPO        default JorreFog/BananaLinux
  DRY_RUN=1          print actions without writing
"""

from __future__ import annotations

import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

JIRA_BASE = os.environ.get("JIRA_BASE_URL", "https://bananalinux.atlassian.net").rstrip("/")
JIRA_EMAIL = os.environ.get("JIRA_EMAIL", "")
JIRA_TOKEN = os.environ.get("JIRA_API_TOKEN", "")
PROJECT = os.environ.get("JIRA_PROJECT_KEY", "KAN")
REPO = os.environ.get("GITHUB_REPO", "JorreFog/BananaLinux")
DRY_RUN = os.environ.get("DRY_RUN", "") in ("1", "true", "yes")
GITHUB_TOKEN = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN") or ""
EPIC_BUGS = os.environ.get("JIRA_EPIC_BUGS", "KAN-97")
EPIC_FEATURES = os.environ.get("JIRA_EPIC_FEATURES", "KAN-98")
EPIC_IMPROVEMENTS = os.environ.get("JIRA_EPIC_IMPROVEMENTS", "KAN-99")
EPIC_PRS = os.environ.get("JIRA_EPIC_PRS", "KAN-100")

HIGHEST = (
    "brick",
    "bootloop",
    "data loss",
    "battery not charging",
    "not charging past",
)
HIGH = (
    "invert",
    "bluetooth",
    "disconnect",
    "doesn't work",
    "doesnt work",
    "crash",
    "broken",
    "big issue",
    "thermal",
    "fast forward",
    "3d resolution",
    "green tint",
)
FEATURE_MARKERS = (
    "feature",
    "request",
    "awesome",
    "would be",
    "is it possible",
    "support",
    "undervolt",
    "mtp",
    "grid layout",
    "folder",
    "homeserver",
)


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(code)


def basic_auth_header(user: str, token: str) -> str:
    import base64

    raw = f"{user}:{token}".encode()
    return "Basic " + base64.b64encode(raw).decode()


def http_json(
    method: str,
    url: str,
    *,
    headers: dict[str, str] | None = None,
    body: Any | None = None,
) -> Any:
    data = None
    req_headers = dict(headers or {})
    if body is not None:
        data = json.dumps(body).encode()
        req_headers.setdefault("Content-Type", "application/json")
    req = urllib.request.Request(url, data=data, headers=req_headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
            if not raw:
                return None
            return json.loads(raw.decode())
    except urllib.error.HTTPError as e:
        detail = e.read().decode(errors="replace")
        raise RuntimeError(f"{method} {url} -> HTTP {e.code}: {detail}") from e


def classify(title: str, body: str) -> tuple[str, str, str, str]:
    """Return (priority, issue_type, kind, epic_key).

    kind is bug | feature | improvement and drives board grouping.
    """
    text = f"{title}\n{body}".lower()
    title_l = title.lower()
    is_feature = (
        any(k in title_l for k in ("feature", "request"))
        or any(
            k in text
            for k in ("feature request", "would be awesome", "is it possible")
        )
        or title_l.startswith(("[feature", "[request"))
    )
    if any(k in text for k in HIGHEST):
        priority = "Highest"
    elif any(k in text for k in HIGH):
        priority = "High"
    elif is_feature or any(k in text for k in FEATURE_MARKERS):
        priority = "Low"
    else:
        priority = "Medium"

    if is_feature:
        return priority, "Story", "feature", EPIC_FEATURES
    return priority, "Task", "bug", EPIC_BUGS


def github_headers() -> dict[str, str]:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "banana-jira-github-sync",
    }
    if GITHUB_TOKEN:
        headers["Authorization"] = f"Bearer {GITHUB_TOKEN}"
    return headers


def github_open_issues() -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    page = 1
    while True:
        qs = urllib.parse.urlencode(
            {"state": "open", "per_page": 100, "page": page}
        )
        url = f"https://api.github.com/repos/{REPO}/issues?{qs}"
        batch = http_json("GET", url, headers=github_headers())
        if not batch:
            break
        for item in batch:
            if "pull_request" in item:
                continue
            issues.append(item)
        if len(batch) < 100:
            break
        page += 1
    return issues


def github_open_pulls() -> list[dict[str, Any]]:
    pulls: list[dict[str, Any]] = []
    page = 1
    while True:
        qs = urllib.parse.urlencode(
            {"state": "open", "per_page": 100, "page": page}
        )
        url = f"https://api.github.com/repos/{REPO}/pulls?{qs}"
        batch = http_json("GET", url, headers=github_headers())
        if not batch:
            break
        pulls.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return pulls


def github_get_pr(number: int) -> dict[str, Any]:
    url = f"https://api.github.com/repos/{REPO}/pulls/{number}"
    return http_json("GET", url, headers=github_headers())


def jira_headers() -> dict[str, str]:
    return {
        "Authorization": basic_auth_header(JIRA_EMAIL, JIRA_TOKEN),
        "Accept": "application/json",
        "User-Agent": "banana-jira-github-sync",
    }


def jira_search(jql: str, fields: str = "key", max_results: int = 100) -> list[dict[str, Any]]:
    params = {"jql": jql, "maxResults": max_results, "fields": fields}
    url = (
        f"{JIRA_BASE}/rest/api/3/search/jql?"
        + urllib.parse.urlencode(params)
    )
    try:
        data = http_json("GET", url, headers=jira_headers())
        return data.get("issues") or []
    except RuntimeError:
        url = (
            f"{JIRA_BASE}/rest/api/3/search?"
            + urllib.parse.urlencode(params)
        )
        data = http_json("GET", url, headers=jira_headers())
        return data.get("issues") or []


def jira_find_by_label(label: str) -> str | None:
    issues = jira_search(
        f'project = {PROJECT} AND labels = "{label}"',
        fields="key",
        max_results=1,
    )
    return issues[0]["key"] if issues else None


def jira_find_by_gh_label(number: int) -> str | None:
    return jira_find_by_label(f"gh-{number}")


def jira_status(key: str) -> str:
    data = http_json(
        "GET",
        f"{JIRA_BASE}/rest/api/3/issue/{key}?fields=status",
        headers=jira_headers(),
    )
    return ((data.get("fields") or {}).get("status") or {}).get("name") or ""


def adf_from_markdownish(text: str) -> dict[str, Any]:
    """Minimal ADF: one paragraph per blank-line-separated block, plain text."""
    blocks = re.split(r"\n\s*\n", text.strip()) or [text]
    content = []
    for block in blocks:
        content.append(
            {
                "type": "paragraph",
                "content": [{"type": "text", "text": block[:4000]}],
            }
        )
    return {"type": "doc", "version": 1, "content": content or [{"type": "paragraph"}]}


def jira_create(
    issue: dict[str, Any],
    priority: str,
    issue_type: str,
    kind: str,
    epic_key: str,
) -> str:
    number = issue["number"]
    title = issue["title"]
    url = issue["html_url"]
    body = (issue.get("body") or "").strip()
    if len(body) > 3500:
        body = body[:3500] + "\n\n(truncated)"
    description = (
        f"Imported from GitHub\n\n"
        f"Source: {url}\n"
        f"GitHub issue: #{number}\n\n"
        f"{body or 'No description'}"
    )
    summary = f"[GH-{number}] {title}"
    if len(summary) > 255:
        summary = summary[:252] + "..."
    payload = {
        "fields": {
            "project": {"key": PROJECT},
            "summary": summary,
            "issuetype": {"name": issue_type},
            "priority": {"name": priority},
            "labels": ["github-import", f"gh-{number}", kind],
            "parent": {"key": epic_key},
            "description": adf_from_markdownish(description),
        }
    }
    if DRY_RUN:
        print(
            f"DRY_RUN create {summary!r} priority={priority} "
            f"type={issue_type} kind={kind} epic={epic_key}"
        )
        return f"DRY-{number}"
    data = http_json(
        "POST",
        f"{JIRA_BASE}/rest/api/3/issue",
        headers=jira_headers(),
        body=payload,
    )
    return data["key"]


def jira_add_remote_link(key: str, url: str, title: str) -> None:
    if DRY_RUN:
        print(f"DRY_RUN link {key} -> {url}")
        return
    payload = {
        "object": {"url": url, "title": title},
        "relationship": "imported from",
    }
    http_json(
        "POST",
        f"{JIRA_BASE}/rest/api/3/issue/{key}/remotelink",
        headers=jira_headers(),
        body=payload,
    )


def jira_transitions(key: str) -> list[dict[str, Any]]:
    data = http_json(
        "GET",
        f"{JIRA_BASE}/rest/api/3/issue/{key}/transitions",
        headers=jira_headers(),
    )
    return data.get("transitions") or []


def jira_transition_to(key: str, wanted: str) -> None:
    if DRY_RUN:
        print(f"DRY_RUN transition {key} -> {wanted}")
        return
    for t in jira_transitions(key):
        name = t.get("name") or ""
        to_name = (t.get("to") or {}).get("name") or ""
        if name == wanted or to_name == wanted:
            http_json(
                "POST",
                f"{JIRA_BASE}/rest/api/3/issue/{key}/transitions",
                headers=jira_headers(),
                body={"transition": {"id": t["id"]}},
            )
            return
    print(f"warn: no {wanted!r} transition for {key}", file=sys.stderr)


def jira_transition_to_github_issues(key: str) -> None:
    jira_transition_to(key, "Github Issues")


def sync_one(issue: dict[str, Any]) -> dict[str, Any]:
    number = issue["number"]
    existing = None if DRY_RUN else jira_find_by_gh_label(number)
    if existing:
        return {"gh": number, "key": existing, "action": "exists"}
    priority, issue_type, kind, epic_key = classify(
        issue["title"], issue.get("body") or ""
    )
    key = jira_create(issue, priority, issue_type, kind, epic_key)
    jira_add_remote_link(key, issue["html_url"], f"GitHub #{number}")
    jira_transition_to_github_issues(key)
    return {
        "gh": number,
        "key": key,
        "action": "created",
        "priority": priority,
        "issueType": issue_type,
        "kind": kind,
        "epic": epic_key,
    }


def pr_target_status(pr: dict[str, Any]) -> str:
    if pr.get("merged_at") or pr.get("state") == "closed":
        return "Done"
    if pr.get("draft"):
        return "In Progress"
    return "In Review"


def jira_create_pr(pr: dict[str, Any]) -> str:
    number = pr["number"]
    title = pr["title"]
    url = pr["html_url"]
    body = (pr.get("body") or "").strip()
    if len(body) > 3500:
        body = body[:3500] + "\n\n(truncated)"
    user = (pr.get("user") or {}).get("login") or "unknown"
    head = ((pr.get("head") or {}).get("ref")) or ""
    base = ((pr.get("base") or {}).get("ref")) or ""
    draft = "yes" if pr.get("draft") else "no"
    priority = "Low" if pr.get("draft") else "Medium"
    description = (
        f"Imported from GitHub pull request\n\n"
        f"Source: {url}\n"
        f"Author: {user}\n"
        f"Branch: `{head}` → `{base}`\n"
        f"Draft: {draft}\n\n"
        f"{body or 'No description'}"
    )
    summary = f"[PR-{number}] {title}"
    if len(summary) > 255:
        summary = summary[:252] + "..."
    payload = {
        "fields": {
            "project": {"key": PROJECT},
            "summary": summary,
            "issuetype": {"name": "Task"},
            "priority": {"name": priority},
            "labels": ["github-pr-import", f"gh-pr-{number}", "pr"],
            "parent": {"key": EPIC_PRS},
            "description": adf_from_markdownish(description),
        }
    }
    if DRY_RUN:
        print(
            f"DRY_RUN create {summary!r} priority={priority} "
            f"type=Task kind=pr epic={EPIC_PRS}"
        )
        return f"DRY-PR-{number}"
    data = http_json(
        "POST",
        f"{JIRA_BASE}/rest/api/3/issue",
        headers=jira_headers(),
        body=payload,
    )
    return data["key"]


def sync_pr(pr: dict[str, Any]) -> dict[str, Any]:
    number = pr["number"]
    target = pr_target_status(pr)
    existing = None if DRY_RUN else jira_find_by_label(f"gh-pr-{number}")
    if existing:
        current = "" if DRY_RUN else jira_status(existing)
        if target == "Done" and current != "Done":
            jira_transition_to(existing, "Done")
            return {"pr": number, "key": existing, "action": "closed"}
        if current == "Done" and target != "Done":
            jira_transition_to(existing, target)
            return {
                "pr": number,
                "key": existing,
                "action": "reopened",
                "status": target,
            }
        return {"pr": number, "key": existing, "action": "exists"}
    if target == "Done":
        return {"pr": number, "action": "skipped-closed"}
    key = jira_create_pr(pr)
    jira_add_remote_link(key, pr["html_url"], f"GitHub PR #{number}")
    jira_transition_to(key, target)
    return {
        "pr": number,
        "key": key,
        "action": "created",
        "status": target,
        "kind": "pr",
        "epic": EPIC_PRS,
    }


def close_merged_pr_cards() -> list[dict[str, Any]]:
    """Move Jira PR cards to Done when the GitHub PR is no longer open."""
    if DRY_RUN:
        return []
    results: list[dict[str, Any]] = []
    cards = jira_search(
        f'project = {PROJECT} AND labels = "github-pr-import" AND status != Done',
        fields="key,labels,status",
    )
    for card in cards:
        labels = (card.get("fields") or {}).get("labels") or []
        match = None
        for label in labels:
            m = re.fullmatch(r"gh-pr-(\d+)", str(label))
            if m:
                match = int(m.group(1))
                break
        if match is None:
            continue
        try:
            pr = github_get_pr(match)
            if pr_target_status(pr) == "Done":
                jira_transition_to(card["key"], "Done")
                rec = {"pr": match, "key": card["key"], "action": "closed"}
                results.append(rec)
                print(json.dumps(rec))
        except Exception as e:  # noqa: BLE001
            err = {
                "pr": match,
                "key": card.get("key"),
                "action": "failed",
                "error": str(e),
            }
            results.append(err)
            print(json.dumps(err), file=sys.stderr)
    return results


def _print_summary(results: list[dict[str, Any]], extra: dict[str, Any] | None = None) -> None:
    created = sum(1 for r in results if r.get("action") == "created")
    existed = sum(1 for r in results if r.get("action") == "exists")
    failed = sum(1 for r in results if r.get("action") == "failed")
    closed = sum(1 for r in results if r.get("action") == "closed")
    payload: dict[str, Any] = {
        "summary": {
            "scanned": len(results),
            "created": created,
            "exists": existed,
            "closed": closed,
            "failed": failed,
        }
    }
    if extra:
        payload["summary"].update(extra)
    print(json.dumps(payload))
    if failed:
        raise SystemExit(1)


def main() -> None:
    only_issue = None
    only_pr = None
    if len(sys.argv) > 1 and sys.argv[1] == "--issue":
        only_issue = int(sys.argv[2])
    elif len(sys.argv) > 1 and sys.argv[1] == "--pr":
        only_pr = int(sys.argv[2])

    if not DRY_RUN and (not JIRA_EMAIL or not JIRA_TOKEN):
        die("set JIRA_EMAIL and JIRA_API_TOKEN")

    results: list[dict[str, Any]] = []

    if only_pr is not None:
        try:
            pr = github_get_pr(only_pr)
        except RuntimeError as e:
            die(f"GitHub PR #{only_pr} not found: {e}")
        try:
            results.append(sync_pr(pr))
            print(json.dumps(results[-1]))
        except Exception as e:  # noqa: BLE001
            err = {"pr": only_pr, "action": "failed", "error": str(e)}
            results.append(err)
            print(json.dumps(err), file=sys.stderr)
        _print_summary(results)
        return

    if only_issue is None:
        pulls = github_open_pulls()
        for pr in sorted(pulls, key=lambda p: p["number"]):
            try:
                results.append(sync_pr(pr))
                print(json.dumps(results[-1]))
            except Exception as e:  # noqa: BLE001
                err = {"pr": pr["number"], "action": "failed", "error": str(e)}
                results.append(err)
                print(json.dumps(err), file=sys.stderr)
        results.extend(close_merged_pr_cards())

    issues = github_open_issues()
    if only_issue is not None:
        issues = [i for i in issues if i["number"] == only_issue]
        if not issues:
            die(f"GitHub issue #{only_issue} not found or not open")

    for issue in sorted(issues, key=lambda i: i["number"]):
        try:
            results.append(sync_one(issue))
            print(json.dumps(results[-1]))
        except Exception as e:  # noqa: BLE001 - keep syncing remaining issues
            err = {"gh": issue["number"], "action": "failed", "error": str(e)}
            results.append(err)
            print(json.dumps(err), file=sys.stderr)

    _print_summary(results)


if __name__ == "__main__":
    main()
