#!/usr/bin/env python3
"""Sync Discord forum posts into the BananaLinux Jira board (project KAN).

Each open forum thread becomes a Jira card in the Custom issues column,
labelled for idempotent re-runs and parented under Bugs / Feature requests.

Required env:
  JIRA_BASE_URL, JIRA_EMAIL, JIRA_API_TOKEN
  DISCORD_BOT_TOKEN
  DISCORD_FORUM_CHANNEL_ID   (right-click the issues forum → Copy Channel ID)

Optional env:
  JIRA_PROJECT_KEY           default KAN
  JIRA_EPIC_BUGS             default KAN-97
  JIRA_EPIC_FEATURES         default KAN-98
  DRY_RUN=1
"""

from __future__ import annotations

import importlib.util
import json
import os
import re
import sys
import time
import urllib.parse
from pathlib import Path
from typing import Any

_GH_PATH = Path(__file__).with_name("jira-github-sync.py")
_SPEC = importlib.util.spec_from_file_location("jira_github_sync", _GH_PATH)
_GH = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
_SPEC.loader.exec_module(_GH)

classify = _GH.classify
http_json = _GH.http_json
jira_headers = _GH.jira_headers
adf_from_markdownish = _GH.adf_from_markdownish
jira_transitions = _GH.jira_transitions
die = _GH.die

JIRA_BASE = _GH.JIRA_BASE
PROJECT = _GH.PROJECT
DRY_RUN = _GH.DRY_RUN
EPIC_BUGS = _GH.EPIC_BUGS
EPIC_FEATURES = _GH.EPIC_FEATURES

DISCORD_TOKEN = os.environ.get("DISCORD_BOT_TOKEN", "")
FORUM_ID = os.environ.get("DISCORD_FORUM_CHANNEL_ID", "").strip()
DISCORD_API = "https://discord.com/api/v10"


def discord_headers() -> dict[str, str]:
    return {
        "Authorization": f"Bot {DISCORD_TOKEN}",
        "User-Agent": "BananaLinux-JiraDiscordSync (https://github.com/JorreFog/BananaLinux)",
        "Accept": "application/json",
    }


def discord_get(path: str, params: dict[str, Any] | None = None) -> Any:
    url = DISCORD_API + path
    if params:
        url += "?" + urllib.parse.urlencode(
            {k: v for k, v in params.items() if v is not None}
        )
    last_error: RuntimeError | None = None
    for attempt in range(5):
        try:
            return http_json("GET", url, headers=discord_headers())
        except RuntimeError as e:
            last_error = e
            msg = str(e)
            if "HTTP 429" in msg and attempt < 4:
                wait = 2 * (attempt + 1)
                match = re.search(r'"retry_after"\s*:\s*([0-9.]+)', msg)
                if match:
                    wait = min(max(float(match.group(1)), 0.5), 30)
                time.sleep(wait)
                continue
            if "HTTP 401" in msg:
                die("Discord rejected the bot token (401). Check DISCORD_BOT_TOKEN.")
            if "HTTP 403" in msg:
                die(
                    "Discord 403: give the bot View Channel and Read Message History "
                    "on the issues forum, then re-invite it if needed."
                )
            raise
    assert last_error is not None
    raise last_error


def tag_kind(forum: dict[str, Any], thread: dict[str, Any]) -> str | None:
    names = {
        str(t["id"]): (t.get("name") or "").lower()
        for t in forum.get("available_tags") or []
    }
    for tag_id in thread.get("applied_tags") or []:
        name = names.get(str(tag_id), "")
        if any(k in name for k in ("feature", "request", "enhancement", "idea")):
            return "feature"
        if any(k in name for k in ("bug", "issue", "broken", "crash")):
            return "bug"
    return None


def classify_discord(
    title: str, body: str, tagged: str | None
) -> tuple[str, str, str, str]:
    priority, issue_type, kind, epic_key = classify(title, body)
    if tagged == "feature":
        return priority, "Story", "feature", EPIC_FEATURES
    if tagged == "bug":
        return priority, "Task", "bug", EPIC_BUGS
    return priority, issue_type, kind, epic_key


def forum_threads(forum: dict[str, Any]) -> list[dict[str, Any]]:
    threads: list[dict[str, Any]] = []
    seen: set[str] = set()

    guild_id = forum.get("guild_id")
    if guild_id:
        active = discord_get(f"/guilds/{guild_id}/threads/active")
        for t in active.get("threads") or []:
            if str(t.get("parent_id")) != str(FORUM_ID):
                continue
            if t.get("thread_metadata", {}).get("archived"):
                continue
            seen.add(str(t["id"]))
            threads.append(t)

    before = None
    while True:
        params: dict[str, Any] = {"limit": 100}
        if before:
            params["before"] = before
        page = discord_get(f"/channels/{FORUM_ID}/threads/archived/public", params)
        batch = page.get("threads") or []
        for t in batch:
            tid = str(t["id"])
            if tid not in seen:
                seen.add(tid)
                threads.append(t)
        if not page.get("has_more") or not batch:
            break
        before = batch[-1]["id"]

    return threads


def starter_message(thread: dict[str, Any]) -> dict[str, Any] | None:
    tid = thread["id"]
    try:
        msg = discord_get(f"/channels/{tid}/messages/{tid}")
        if isinstance(msg, dict):
            return msg
    except RuntimeError:
        pass
    try:
        msgs = discord_get(f"/channels/{tid}/messages", {"limit": 1})
        if msgs:
            return msgs[0]
    except RuntimeError:
        return None
    return None


def author_line(thread: dict[str, Any], starter: dict[str, Any] | None) -> str:
    if starter:
        author = starter.get("author") or {}
        name = author.get("global_name") or author.get("username") or ""
        uid = str(author.get("id") or "")
        if name and uid:
            return f"Author: {name} ({uid})"
        if name:
            return f"Author: {name}"
        if uid:
            return f"Author Discord ID: {uid}"
    owner_id = thread.get("owner_id") or ""
    if owner_id:
        return f"Author Discord ID: {owner_id}"
    return "Author: unknown"


def jira_find_by_dc_label(thread_id: str) -> str | None:
    jql = f'project = {PROJECT} AND labels = "dc-{thread_id}"'
    url = (
        f"{JIRA_BASE}/rest/api/3/search/jql?"
        + urllib.parse.urlencode({"jql": jql, "maxResults": 1, "fields": "key"})
    )
    try:
        data = http_json("GET", url, headers=jira_headers())
        issues = data.get("issues") or []
        return issues[0]["key"] if issues else None
    except RuntimeError:
        url = (
            f"{JIRA_BASE}/rest/api/3/search?"
            + urllib.parse.urlencode({"jql": jql, "maxResults": 1, "fields": "key"})
        )
        data = http_json("GET", url, headers=jira_headers())
        issues = data.get("issues") or []
        return issues[0]["key"] if issues else None


def jira_create(
    thread: dict[str, Any],
    body: str,
    jump: str,
    tagged: str | None,
    author: str,
) -> dict[str, Any]:
    title = (thread.get("name") or "Untitled Discord post").strip()
    priority, issue_type, kind, epic_key = classify_discord(title, body, tagged)
    tid = str(thread["id"])
    summary = f"[DC] {title}"
    if len(summary) > 255:
        summary = summary[:252] + "..."
    description = (
        f"Imported from Discord\n\n"
        f"Source: {jump}\n"
        f"Thread ID: {tid}\n"
        f"{author}\n\n"
        f"{body or 'No description'}"
    )
    payload = {
        "fields": {
            "project": {"key": PROJECT},
            "summary": summary,
            "issuetype": {"name": issue_type},
            "priority": {"name": priority},
            "labels": ["discord-import", f"dc-{tid}", kind],
            "parent": {"key": epic_key},
            "description": adf_from_markdownish(description[:8000]),
        }
    }
    if DRY_RUN:
        print(
            f"DRY_RUN create {summary!r} priority={priority} "
            f"type={issue_type} kind={kind} epic={epic_key}"
        )
        return {
            "dc": tid,
            "key": f"DRY-{tid}",
            "action": "created",
            "priority": priority,
            "kind": kind,
        }
    data = http_json(
        "POST",
        f"{JIRA_BASE}/rest/api/3/issue",
        headers=jira_headers(),
        body=payload,
    )
    key = data["key"]
    jira_add_remote_link(key, jump, f"Discord: {title[:80]}")
    jira_transition_to_custom_issues(key)
    return {
        "dc": tid,
        "key": key,
        "action": "created",
        "priority": priority,
        "kind": kind,
        "issueType": issue_type,
        "epic": epic_key,
    }


def jira_add_remote_link(key: str, url: str, title: str) -> None:
    if DRY_RUN:
        return
    http_json(
        "POST",
        f"{JIRA_BASE}/rest/api/3/issue/{key}/remotelink",
        headers=jira_headers(),
        body={"object": {"url": url, "title": title}, "relationship": "imported from"},
    )


def jira_transition_to_custom_issues(key: str) -> None:
    if DRY_RUN:
        print(f"DRY_RUN transition {key} -> Custom issues")
        return
    wanted = ("Custom issues", "Github Issues")
    for t in jira_transitions(key):
        name = t.get("name") or ""
        to_name = (t.get("to") or {}).get("name") or ""
        if name in wanted or to_name in wanted:
            http_json(
                "POST",
                f"{JIRA_BASE}/rest/api/3/issue/{key}/transitions",
                headers=jira_headers(),
                body={"transition": {"id": t["id"]}},
            )
            return
    print(f"warn: no Custom issues transition for {key}", file=sys.stderr)


def sync_thread(forum: dict[str, Any], thread: dict[str, Any]) -> dict[str, Any]:
    tid = str(thread["id"])
    existing = None if DRY_RUN else jira_find_by_dc_label(tid)
    if existing:
        return {"dc": tid, "key": existing, "action": "exists"}
    starter = starter_message(thread)
    body = ((starter or {}).get("content") or "").strip()
    tagged = tag_kind(forum, thread)
    guild_id = forum.get("guild_id") or thread.get("guild_id") or ""
    jump = f"https://discord.com/channels/{guild_id}/{tid}"
    return jira_create(thread, body, jump, tagged, author_line(thread, starter))


def main() -> None:
    if not DISCORD_TOKEN or not FORUM_ID:
        die("set DISCORD_BOT_TOKEN and DISCORD_FORUM_CHANNEL_ID")
    if not DRY_RUN and (
        not os.environ.get("JIRA_EMAIL") or not os.environ.get("JIRA_API_TOKEN")
    ):
        die("set JIRA_EMAIL and JIRA_API_TOKEN")

    try:
        forum = discord_get(f"/channels/{FORUM_ID}")
    except RuntimeError as e:
        if "HTTP 404" in str(e):
            die(
                f"Discord 404 for forum {FORUM_ID}. Check DISCORD_FORUM_CHANNEL_ID "
                "and that the bot is in the server."
            )
        raise
    if forum.get("type") not in (15, 16):  # 15=forum, 16=media forum
        print(
            f"warn: channel {FORUM_ID} type={forum.get('type')} "
            "(15 is a forum). Continuing anyway.",
            file=sys.stderr,
        )

    threads = forum_threads(forum)
    results = []
    for thread in threads:
        if thread.get("thread_metadata", {}).get("archived"):
            continue
        try:
            results.append(sync_thread(forum, thread))
            print(json.dumps(results[-1]))
        except Exception as e:  # noqa: BLE001
            err = {"dc": thread.get("id"), "action": "failed", "error": str(e)}
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
