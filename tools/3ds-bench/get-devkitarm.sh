#!/usr/bin/env bash
# Fetch the devkitpro/devkitarm Docker image WITHOUT a docker daemon, using the
# registry HTTP API with curl, and extract its layers into a directory.
#
# Usage: ./get-devkitarm.sh [DEST]      (default DEST=/home/user/devkitpro)
#
# Result: $DEST/rootfs/opt/devkitpro/{devkitARM,libctru,tools,...}
# Then:   export DEVKITPRO=$DEST/rootfs/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM
#
# Keep the toolchain OUTSIDE the BananaLinux repo.
set -euo pipefail

DEST=${1:-/home/user/devkitpro}
REPO=devkitpro/devkitarm
TAG=${TAG:-latest}
ARCH=${ARCH:-amd64}
REG=https://registry-1.docker.io/v2/$REPO

mkdir -p "$DEST/blobs" "$DEST/rootfs"

token() {
    curl -fsS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:$REPO:pull" |
        python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])'
}
TOKEN=$(token)
ACCEPT="application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.v2+json"

echo ">> manifest list $REPO:$TAG"
curl -fsS -H "Authorization: Bearer $TOKEN" -H "Accept: $ACCEPT" "$REG/manifests/$TAG" > "$DEST/index.json"
DIGEST=$(python3 -c '
import sys,json
d=json.load(open(sys.argv[1]))
if "manifests" not in d: print(""); sys.exit()
for m in d["manifests"]:
    p=m.get("platform",{})
    if p.get("os")=="linux" and p.get("architecture")==sys.argv[2]:
        print(m["digest"]); break
' "$DEST/index.json" "$ARCH")
if [ -n "$DIGEST" ]; then
    echo ">> $ARCH manifest $DIGEST"
    curl -fsS -H "Authorization: Bearer $TOKEN" -H "Accept: $ACCEPT" "$REG/manifests/$DIGEST" > "$DEST/manifest.json"
else
    cp "$DEST/index.json" "$DEST/manifest.json"
fi

LAYERS=$(python3 -c 'import sys,json;[print(l["digest"]) for l in json.load(open(sys.argv[1]))["layers"]]' "$DEST/manifest.json")

for L in $LAYERS; do
    f="$DEST/blobs/${L#sha256:}"
    if [ -f "$f" ] && echo "${L#sha256:}  $f" | sha256sum -c --status; then
        echo ">> have $L"; continue
    fi
    echo ">> fetch $L"
    TOKEN=$(token)   # tokens expire after ~5 min; big layers take a while
    curl -fsSL --retry 3 -H "Authorization: Bearer $TOKEN" "$REG/blobs/$L" -o "$f"
    echo "${L#sha256:}  $f" | sha256sum -c --status || { echo "checksum mismatch $L"; exit 1; }
done

for L in $LAYERS; do
    f="$DEST/blobs/${L#sha256:}"
    echo ">> extract $L"
    # apply whiteouts of this layer (delete paths from lower layers)
    tar -tf "$f" 2>/dev/null | grep -E '(^|/)\.wh\.' | while read -r w; do
        base=$(basename "$w"); dir=$(dirname "$w")
        if [ "$base" = ".wh..wh..opq" ]; then
            find "$DEST/rootfs/$dir" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
        else
            rm -rf "$DEST/rootfs/$dir/${base#.wh.}"
        fi
    done || true
    # device nodes and ownership changes may fail when not root; ignore
    tar -xf "$f" -C "$DEST/rootfs" --no-same-owner --exclude='.wh.*' \
        --exclude='dev/*' 2>/dev/null || true
done

DKP="$DEST/rootfs/opt/devkitpro"
test -x "$DKP/devkitARM/bin/arm-none-eabi-gcc" || { echo "devkitARM missing"; exit 1; }
cat > "$DEST/env.sh" <<EOF
export DEVKITPRO=$DKP
export DEVKITARM=$DKP/devkitARM
export PATH=$DKP/tools/bin:$DKP/devkitARM/bin:\$PATH
EOF
echo ">> done. source $DEST/env.sh"
"$DKP/devkitARM/bin/arm-none-eabi-gcc" --version | head -1
