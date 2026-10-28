#!/usr/bin/env node
// Fetches every issue of the repository (pull requests excluded) from the
// GitHub REST API and writes a slim snapshot to site/data.json.
//
// Usage:
//   GITHUB_TOKEN=... GITHUB_REPOSITORY=JorreFog/BananaLinux node scripts/generate-dashboard-data.mjs
//
// GITHUB_TOKEN is optional for public repositories, but without it the API
// rate limit is very low (60 requests per hour).

import { mkdir, writeFile } from "node:fs/promises";

const repo = process.env.GITHUB_REPOSITORY || "JorreFog/BananaLinux";
const token = process.env.GITHUB_TOKEN;
const outFile = new URL("../site/data.json", import.meta.url);

const headers = {
  Accept: "application/vnd.github+json",
  "X-GitHub-Api-Version": "2022-11-28",
  "User-Agent": "dashboard-data-generator",
};
if (token) headers.Authorization = `Bearer ${token}`;

// Follows the "Link: <...>; rel=next" header until there are no more pages.
async function fetchAll(firstUrl) {
  const items = [];
  let url = firstUrl;
  while (url) {
    const res = await fetch(url, { headers });
    if (!res.ok) {
      throw new Error(`GitHub API error ${res.status} for ${url}: ${await res.text()}`);
    }
    items.push(...(await res.json()));
    const link = res.headers.get("link") || "";
    const next = link.split(",").find((part) => part.includes('rel="next"'));
    url = next ? next.slice(next.indexOf("<") + 1, next.indexOf(">")) : null;
  }
  return items;
}

const raw = await fetchAll(
  `https://api.github.com/repos/${repo}/issues?state=all&per_page=100&sort=created&direction=desc`
);

// The bug report form stores the answer under a "### BananaLinux version" heading.
// Returns a version such as "0.5.0", the raw text if no number is found, or null.
function extractBuildVersion(body) {
  const match = /###\s*BananaLinux version\s*\r?\n+([^\r\n]+)/i.exec(body || "");
  if (!match) return null;
  const text = match[1].trim();
  const version = /\d+(?:\.\d+)+(?:-[\w.]+)?/.exec(text);
  if (version) return version[0];
  return text === "_No response_" ? null : text.slice(0, 30);
}

const issues = raw
  .filter((item) => !item.pull_request) // the issues endpoint also returns PRs
  .map((item) => ({
    number: item.number,
    title: item.title,
    url: item.html_url,
    state: item.state,
    created_at: item.created_at,
    updated_at: item.updated_at,
    closed_at: item.closed_at,
    comments: item.comments,
    build_version: extractBuildVersion(item.body),
    labels: item.labels.map((l) => ({ name: l.name, color: l.color })),
  }));

await mkdir(new URL("./", outFile), { recursive: true });
await writeFile(
  outFile,
  JSON.stringify({ repo, generated_at: new Date().toISOString(), issues })
);

console.log(`Wrote ${issues.length} issues to site/data.json`);
