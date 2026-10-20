#!/usr/bin/env python3
"""The Store's package manager in BananaOS mode, on the PC: a stand-in handheld under a temporary folder, GitHub's
answers as files, eReaderDS as a made-up release. Checks where everything lands (/data/store/apps, the Apps shelf's
/data/store/tiles), that the catalog that came with the system is used while Banana's isn't on the network, and that
a remove leaves the shelf's folder and a port of the same name alone.
  python3 store/test-banana.py <rocknixds-store, with BananaOS's patches>"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main(cli):
    t = tempfile.mkdtemp(prefix="banana-store-")
    try:
        root = os.path.join(t, "root")
        os.makedirs(root + "/etc/banana")
        with open(root + "/etc/banana/version", "w") as f:
            f.write("0.4.2\n")
        os.makedirs(root + "/data/roms/ports")
        with open(root + "/data/roms/ports/eReaderDS.sh", "w") as f:
            f.write("the user's own port\n")
        os.makedirs(root + "/data/store/tiles")
        # the Store as the package installs it: the tool, the catalog and its pictures beside it
        share = os.path.join(t, "share")
        shutil.copytree(os.path.join(HERE, "apps"), os.path.join(share, "apps"))
        shutil.copy(os.path.join(HERE, "catalog.json"), share)
        shutil.copy(cli, os.path.join(share, "rocknixds-store"))
        # eReaderDS 1.2.3 as a release
        z = os.path.join(t, "dl", "v1.2.3", "eReaderDS-v1.2.3.zip")
        os.makedirs(os.path.dirname(z))
        with zipfile.ZipFile(z, "w") as zf:
            zf.writestr("Ports/eReaderDS.sh", "#!/bin/bash\nexec \"$(dirname \"$0\")/eReaderDS/launch.sh\"\n")
            zf.writestr("Ports/eReaderDS/launch.sh", "#!/bin/bash\necho reading\n")
            zf.writestr("Ports/eReaderDS/app/version.lua", 'return "1.2.3"\n')
            zf.writestr("Ports/eReaderDS/runtime/love.aarch64", "elf\n")
            zf.write(os.path.join(HERE, "apps/ereaderds/icon.png"), "Ports/eReaderDS/icon.png")
        digest = hashlib.sha256(open(z, "rb").read()).hexdigest()
        api = os.path.join(t, "api", "casualducko", "eReaderDS.json")
        os.makedirs(os.path.dirname(api))
        with open(api, "w") as f:
            json.dump([{"tag_name": "v1.2.3", "body": "## New\n- pages", "assets": [
                {"name": "eReaderDS-v1.2.3.zip", "size": os.path.getsize(z), "digest": "sha256:" + digest}]}], f)
        env = dict(os.environ, RNDS_STORE_PLATFORM="banana", RNDS_STORE_ROOT=root, RNDS_STORE_DEVICE="rgds-plus",
                   RNDS_STORE_MAIN_CATALOG="file://" + t + "/nowhere/catalog.json",
                   RNDS_STORE_API="file://" + t + "/api/{repo}.json",
                   RNDS_STORE_DOWNLOAD="file://" + t + "/dl/{tag}/{asset}")
        env.pop("RNDS_STORE_DATA", None)

        def run(*a):
            r = subprocess.run([sys.executable, os.path.join(share, "rocknixds-store")] + list(a), env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            return r.stdout

        fails = []

        def check(ok, what):
            print(("ok   " if ok else "FAIL ") + what)
            if not ok:
                fails.append(what)

        o = run("refresh")
        check(o.strip().endswith("OK"), "refresh with the catalog that came with the system: " + o.strip())
        check(os.path.exists(root + "/data/store/data/latest.json"), "the cache in /data/store/data")
        check(os.path.exists(root + "/data/store/data/media/ereaderds/icon.raw"), "the icon from the catalog beside it")
        o = run("list")
        check("\tereaderds\t" in o and "\tinstall\t" in o, "list: eReaderDS to install")
        o = run("install", "ereaderds")
        check(o.strip().splitlines()[-1].startswith("DONE"), "install: " + o.strip().splitlines()[-1])
        app = root + "/data/store/apps/ereaderds"
        check(os.path.isfile(app + "/eReaderDS/launch.sh"), "the app in /data/store/apps/ereaderds")
        entry = root + "/data/store/tiles/eReaderDS.sh"
        text = open(entry).read() if os.path.exists(entry) else ""
        check("exec " in text and app + "/eReaderDS.sh" in text and "swaymsg" not in text,
              "the Apps shelf's entry starts the app's own script, no sway")
        gl = open(root + "/data/store/tiles/gamelist.xml").read() if os.path.exists(root + "/data/store/tiles/gamelist.xml") else ""
        check("<path>./eReaderDS.sh</path>" in gl and "<image>./images/icon.png</image>" in gl,
              "its line and picture in the shelf's gamelist.xml")
        check(open(root + "/data/roms/ports/eReaderDS.sh").read() == "the user's own port\n", "the user's port untouched")
        check(not os.path.exists(root + "/storage"), "nothing under /storage")
        o = run("list")
        check("\t1.2.3\t" in o and "\tinstalled\t" in o, "list: 1.2.3 installed")
        o = run("remove", "ereaderds")
        check(o.strip().endswith("DONE"), "remove: " + o.strip())
        check(not os.path.exists(entry) and not os.path.exists(app), "the entry and the app gone")
        check(os.path.isdir(root + "/data/store/tiles"), "the shelf's folder stays (the menu watches it)")
        check(open(root + "/data/roms/ports/eReaderDS.sh").read() == "the user's own port\n", "the user's port still there")
        # no network at all: OFFLINE, and what it knew is kept
        env["RNDS_STORE_API"] = "file://" + t + "/nowhere/{repo}.json"
        o = run("refresh")
        check(o.strip() == "OFFLINE", "refresh with no network: " + o.strip())
        print("banana store: " + ("OK" if not fails else "%d FAILED" % len(fails)))
        return 1 if fails else 0
    finally:
        shutil.rmtree(t, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
