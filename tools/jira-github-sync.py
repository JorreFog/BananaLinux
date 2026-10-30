#!/usr/bin/env python3
"""Sync open GitHub issues into the BananaLinux Jira board (project KAN).

Creates missing Jira issues with auto-assigned priority, labels them for
idempotent re-runs, links back to GitHub, and moves them into the
"Github Issues" board column.

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


def github_open_issues() -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    page = 1
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "banana-jira-github-sync",
    }
    if GITHUB_TOKEN:
        headers["Authorization"] = f"Bearer {GITHUB_TOKEN}"
    while True:
        qs = urllib.parse.urlencode(
            {"state": "open", "per_page": 100, "page": page}
        )
        url = f"https://api.github.com/repos/{REPO}/issues?{qs}"
        batch = http_json("GET", url, headers=headers)
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


def jira_headers() -> dict[str, str]:
    return {
        "Authorization": basic_auth_header(JIRA_EMAIL, JIRA_TOKEN),
        "Accept": "application/json",
        "User-Agent": "banana-jira-github-sync",
    }


def jira_find_by_gh_label(number: int) -> str | None:
    jql = f'project = {PROJECT} AND labels = "gh-{number}"'
    url = (
        f"{JIRA_BASE}/rest/api/3/search/jql?"
        + urllib.parse.urlencode(
            {"jql": jql, "maxResults": 1, "fields": "key"}
        )
    )
    # Prefer new search endpoint; fall back to classic /search.
    try:
        data = http_json("GET", url, headers=jira_headers())
        issues = data.get("issues") or []
        if issues:
            return issues[0]["key"]
        return None
    except RuntimeError:
        url = (
            f"{JIRA_BASE}/rest/api/3/search?"
            + urllib.parse.urlencode(
                {"jql": jql, "maxResults": 1, "fields": "key"}
            )
        )
        data = http_json("GET", url, headers=jira_headers())
        issues = data.get("issues") or []
        return issues[0]["key"] if issues else None


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


def jira_transition_to_github_issues(key: str) -> None:
    if DRY_RUN:
        print(f"DRY_RUN transition {key} -> Github Issues")
        return
    for t in jira_transitions(key):
        if t.get("name") == "Github Issues" or (t.get("to") or {}).get("name") == "Github Issues":
            http_json(
                "POST",
                f"{JIRA_BASE}/rest/api/3/issue/{key}/transitions",
                headers=jira_headers(),
                body={"transition": {"id": t["id"]}},
            )
            return
    print(f"warn: no Github Issues transition for {key}", file=sys.stderr)


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


def main() -> None:
    only = None
    if len(sys.argv) > 1 and sys.argv[1] == "--issue":
        only = int(sys.argv[2])

    if not DRY_RUN and (not JIRA_EMAIL or not JIRA_TOKEN):
        die("set JIRA_EMAIL and JIRA_API_TOKEN")

    issues = github_open_issues()
    if only is not None:
        issues = [i for i in issues if i["number"] == only]
        if not issues:
            die(f"GitHub issue #{only} not found or not open")

    results = []
    for issue in sorted(issues, key=lambda i: i["number"]):
        try:
            results.append(sync_one(issue))
            print(json.dumps(results[-1]))
        except Exception as e:  # noqa: BLE001 - keep syncing remaining issues
            err = {"gh": issue["number"], "action": "failed", "error": str(e)}
            results.append(err)
            print(json.dumps(err), file=sys.stderr)

    created = sum(1 for r in results if r.get("action") == "created")
    existed = sum(1 for r in results if r.get("action") == "exists")
    failed = sum(1 for r in results if r.get("action") == "failed")
    print(
        json.dumps(
            {
                "summary": {
                    "scanned": len(results),
                    "created": created,
                    "exists": existed,
                    "failed": failed,
                }
            }
        )
    )
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
