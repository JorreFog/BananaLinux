#!/bin/sh
# A stand-in for GitHub's release list (#69's test): fake-releases.sh <dir> [bad-sum] [base]. <dir>/feed.json is laid
# out as api.github.com/repos/.../releases answers (authors, uploaders, notes with quotes and a "key" in them), its
# download URLs <base>files/... (default file://<dir>/: fake-http.py serves <dir> over HTTP):
#   v9.9.9  a draft, with packages for both (never offered)
#   v0.5.0  a pre-release with the RG DS Plus's package only (the RG DS skips it)
#   v0.4.3  a pre-release for both; bad-sum: the RG DS's SHA256SUMS has the wrong hash for its package
#   v0.4.2  a release for both
# bad-sum "beta": the beta channel's list instead (the BananaOS site's): v0.5.0-beta.2 and v0.5.0, pre-releases for
# the RG DS Plus
# Each package is a small tar (VERSION, board, 1.5 MB of noise): a real download, not a real system.
set -e
D=$1; BAD=${2:-}; BASE=${3:-}
rm -rf "$D"; mkdir -p "$D/files"
python3 -I - "$D" "$BAD" "$BASE" <<'PY'
import hashlib, io, json, os, random, sys, tarfile
d, bad, base = sys.argv[1], sys.argv[2], sys.argv[3] or 'file://%s/' % os.path.abspath(sys.argv[1])
random.seed(69)
def pkg(path, board, ver, size):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode='w') as t:
        for name, data in (('VERSION', ver + '\n'), ('board', board + '\n')):
            data = data.encode(); i = tarfile.TarInfo('banana-%s-%s/%s' % (board, ver, name)); i.size = len(data); t.addfile(i, io.BytesIO(data))
        noise = bytes(random.getrandbits(8) for _ in range(size))
        i = tarfile.TarInfo('banana-%s-%s/noise' % (board, ver)); i.size = len(noise); t.addfile(i, io.BytesIO(noise))
    open(path, 'wb').write(b.getvalue())
    return len(b.getvalue()), hashlib.sha256(b.getvalue()).hexdigest()
uploader = {"login": "JorreFog", "id": 1, "node_id": "U_x", "type": "User", "site_admin": False}
releases = []
BETA = (("v0.5.0-beta.2", True, False, ("rgds-plus",), "BananaOS 0.5.0 beta 2."),
        ("v0.5.0", True, False, ("rgds-plus",), "BananaOS 0.5.0 beta 1."))
if bad == 'beta': bad = ''
else: BETA = None
for tag, pre, draft, boards, body in BETA or (
        ("v9.9.9", True, True, ("rgds", "rgds-plus"), "a draft"),
        ("v0.5.0", True, False, ("rgds-plus",), "**BananaOS 0.5.0** for the RG DS Plus only."),
        ("v0.4.3", True, False, ("rgds", "rgds-plus"),
         "**BananaOS 0.4.3 (alpha)** for the RG DS and RG DS Plus: \"updates over Wi-Fi\" ([#69](https://example.invalid/69)),\n"
         "a faster \"tag_name\": \"v99.0.0\" menu.\n\n## Fixed\n- everything else"),
        ("v0.4.2", False, False, ("rgds", "rgds-plus"), "BananaOS 0.4.2.")):
    v = tag[1:]
    os.makedirs('%s/files/%s' % (d, tag))
    assets = []
    for board in boards:
        name = 'banana-%s-%s.update.tar' % (board, v)
        size, h = pkg('%s/files/%s/%s' % (d, tag, name), board, v, 1500000 if v == '0.4.3' else 20000)
        if bad and board == 'rgds' and v == '0.4.3': h = '0' * 64
        sums = 'SHA256SUMS-%s.txt' % board
        open('%s/files/%s/%s' % (d, tag, sums), 'w').write('%s  banana-%s.img.gz\n%s  %s\n' % ('1' * 64, board, h, name))
        for n in (name, sums):
            assets.append({"url": "https://api.github.invalid/assets/1", "id": 1, "name": n, "label": "", "uploader": uploader,
                "content_type": "application/x-tar", "state": "uploaded", "size": os.path.getsize('%s/files/%s/%s' % (d, tag, n)),
                "download_count": 3, "browser_download_url": '%sfiles/%s/%s' % (base, tag, n)})
    releases.append({"url": "https://api.github.invalid/releases/1", "html_url": "https://github.invalid/" + tag, "id": 1,
        "author": uploader, "node_id": "RE_x", "tag_name": tag, "target_commitish": "main", "name": "BananaOS " + v,
        "draft": draft, "immutable": False, "prerelease": pre, "created_at": "2026-10-22T13:13:27Z", "assets": assets,
        "tarball_url": None, "body": body})
json.dump(releases, open(d + '/feed.json', 'w'), indent=2)
PY
