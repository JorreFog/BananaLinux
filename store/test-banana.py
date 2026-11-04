#!/usr/bin/env python3
"""The Store's package manager in BananaOS mode, on the PC: a stand-in handheld under a temporary folder, GitHub's
answers as files, eReaderDS as a made-up release. Checks where everything lands (/data/store/apps, the Apps shelf's
/data/store/tiles), that the catalog that came with the system is used while Banana's isn't on the network, that
a remove leaves the shelf's folder and a port of the same name alone, and that the apps that come with the system
(Döda Kvarter, Poketool) are listed as installed, a remove takes their tile off the menu
(/data/store/hidden-systems) and an install puts it back.
  python3 store/test-banana.py <rocknixds-store, with BananaOS's patches>"""
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
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
        # Döda Kvarter's folder in the system with its VERSION; Poketool's without one (the system's version then)
        os.makedirs(root + "/usr/share/banana/apps/dodakvarter")
        os.makedirs(root + "/usr/share/banana/apps/bank")
        os.makedirs(root + "/usr/share/banana/apps/bshare")        # (Bshare: banana-ui's own page, its tile's folder)
        with open(root + "/usr/share/banana/apps/dodakvarter/VERSION", "w") as f:
            f.write("0.3.0\n")
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
        def row(i):
            return next((ln.split("\t") for ln in run("list").splitlines() if ln.startswith("APP\t%s\t" % i)), [])

        # GBA DS (kyannon/gba-ds): its release zip as it is (GBA DS.sh, gba-ds/, images/ at the top), the app's folder
        # alone installed, its version from licenses/README.txt (it updates itself), its states and settings kept
        env["RNDS_STORE_DEVICE"] = "rgds"

        def gbads_zip(v):
            z = os.path.join(t, "dl", "v" + v, "gba-ds-v%s-linux-aarch64.zip" % v)
            os.makedirs(os.path.dirname(z))
            with zipfile.ZipFile(z, "w") as zf:
                zf.writestr("GBA DS.sh", "#!/bin/bash\nexec bash \"$(dirname \"$0\")/gba-ds/launch.sh\" \"$@\"\n")
                zf.writestr("images/GBA DS.png", "png\n")
                zf.writestr("gba-ds/gba-ds", "elf %s\n" % v)
                zf.writestr("gba-ds/launch.sh", "#!/bin/bash\necho gba\n")
                zf.writestr("gba-ds/banana-play.sh", "#!/bin/bash\necho play\n")
                zf.writestr("gba-ds/gba-ds.cfg", "rom_dir = /storage/roms/gba\nshow_all = 0\n")
                zf.writestr("gba-ds/cheats/README.txt", "cheats\n")
                zf.writestr("gba-ds/licenses/README.txt", "GBA DS %s is free software under the GNU General Public "
                            "License, version 3 only (GBA-DS-LICENSE.txt).\n" % v)
            return {"name": os.path.basename(z), "size": os.path.getsize(z),
                    "digest": "sha256:" + hashlib.sha256(open(z, "rb").read()).hexdigest()}

        rels = [{"tag_name": "v" + v, "body": "GBA DS " + v, "assets": [gbads_zip(v)]} for v in ("1.4.7", "1.4.6")]
        api = os.path.join(t, "api", "kyannon", "gba-ds.json")
        os.makedirs(os.path.dirname(api))
        with open(api, "w") as f:
            json.dump(rels, f)
        o = run("refresh")
        check(o.strip().endswith("OK"), "refresh with GBA DS's releases: " + o.strip())
        o = run("install", "gbads", "--version", "1.4.6")
        check(o.strip().splitlines()[-1] == "DONE 1.4.6", "install GBA DS 1.4.6: " + o.strip().splitlines()[-1])
        gb = root + "/data/store/apps/gbads"
        check(os.path.isfile(gb + "/gba-ds") and os.path.isfile(gb + "/launch.sh") and os.access(gb + "/gba-ds", os.X_OK),
              "GBA DS's folder in /data/store/apps/gbads, its program runnable")
        check(not os.path.exists(gb + "/gba-ds/gba-ds") and not os.path.exists(gb + "/GBA DS.sh"),
              "only the app's own folder (gba-ds/), not the Ports entry beside it")
        entry = root + "/data/store/tiles/GBADS.sh"
        text = open(entry).read() if os.path.exists(entry) else ""
        check("exec '%s/launch.sh'" % gb in text, "the Apps shelf's entry starts its launch.sh")
        check("<path>./GBADS.sh</path>" in open(root + "/data/store/tiles/gamelist.xml").read(), "its line in the shelf's list")
        r = row("gbads")
        check(r[5:8] == ["1.4.6", "1.4.7", "update"], "list: 1.4.6 installed, 1.4.7 offered: %s" % r[5:8])
        # the player's states and settings, then an update
        os.makedirs(gb + "/states")
        with open(gb + "/states/Game.auto.state", "w") as f:
            f.write("state\n")
        with open(gb + "/gba-ds.cfg", "w") as f:
            f.write("show_all = 1\n")
        o = run("update", "gbads")
        check(o.strip().splitlines()[-1] == "DONE 1.4.7", "update GBA DS: " + o.strip().splitlines()[-1])
        check(open(gb + "/gba-ds").read() == "elf 1.4.7\n", "the new program")
        check(open(gb + "/states/Game.auto.state").read() == "state\n" and open(gb + "/gba-ds.cfg").read() == "show_all = 1\n",
              "the update keeps its states and the player's gba-ds.cfg")
        # it updated itself (its own updater writes the new files over the folder): the Store sees the new version
        with open(gb + "/licenses/README.txt", "w") as f:
            f.write("GBA DS 1.4.8 is free software under the GNU General Public License, version 3 only.\n")
        check(row("gbads")[5:8] == ["1.4.8", "1.4.7", "installed"], "updated by itself: 1.4.8 installed, nothing offered")
        o = run("remove", "gbads")
        check(o.strip().endswith("DONE") and not os.path.exists(entry) and not os.path.exists(gb + "/gba-ds"),
              "remove GBA DS: its tile and program gone")
        check(os.path.isfile(gb + "/states/Game.auto.state") and os.path.isfile(gb + "/gba-ds.cfg"),
              "its states and settings stay")
        check(row("gbads")[7:8] == ["install"], "list: GBA DS to install again")
        env["RNDS_STORE_DEVICE"] = "rgds-plus"
        # the apps that come with the system
        hidden = root + "/data/store/hidden-systems"

        r = row("dodakvarter")
        check(r[5:8] == ["0.3.0", "0.3.0", "installed"], "list: Döda Kvarter 0.3.0 installed (its VERSION): %s" % r[5:8])
        r = row("bank")
        check(r[5:8] == ["0.4.2", "0.4.2", "installed"], "list: Poketool installed (the system's version): %s" % r[5:8])
        o = run("remove", "dodakvarter")
        check(o.strip().endswith("DONE"), "remove Döda Kvarter: " + o.strip())
        lines = open(hidden).read().split() if os.path.exists(hidden) else []
        check("dodakvarter" in lines and "rocknixdsbank" not in lines, "its system in hidden-systems, Bank's not")
        check(os.path.isfile(root + "/usr/share/banana/apps/dodakvarter/VERSION"), "nothing of it deleted")
        r = row("dodakvarter")
        check(r[5:8] == ["", "0.3.0", "install"], "list: Döda Kvarter to install: %s" % r[5:8])
        o = run("refresh")
        check(o.strip().endswith("OK"), "refresh with one taken off: " + o.strip())
        o = run("install", "dodakvarter")
        check(o.strip().splitlines()[-1] == "DONE 0.3.0", "install Döda Kvarter: " + o.strip().splitlines()[-1])
        check(os.path.exists(hidden) and "dodakvarter" not in open(hidden).read().split(), "its system out of hidden-systems")
        check(row("dodakvarter")[7:8] == ["installed"], "list: Döda Kvarter installed again")
        # Bshare (banana-ui's own page): installed with the system's version, removed and put back the same way
        r = row("bshare")
        check(r[5:8] == ["0.4.2", "0.4.2", "installed"], "list: Bshare installed (the system's version): %s" % r[5:8])
        o = run("remove", "bshare")
        check(o.strip().endswith("DONE") and "bshare" in open(hidden).read().split(), "remove Bshare: its system in hidden-systems")
        o = run("install", "bshare")
        check(o.strip().splitlines()[-1] == "DONE 0.4.2" and "bshare" not in open(hidden).read().split(), "install Bshare: its system out of hidden-systems")
        env["RNDS_STORE_DEVICE"] = "rgds"
        check(row("bank")[7:8] == ["installed"] and row("ereaderds")[7:8] == ["unsupported"],
              "the RG DS: Poketool installed, eReaderDS not for it")
        env["RNDS_STORE_DEVICE"] = "rgds-plus"
        # a line written by hand, as banana-ui reads it (case, spaces): hidden for the Store too, and install clears it
        with open(hidden, "w") as f:
            f.write("  # a note\n  DodaKvarter  \n")
        check(row("dodakvarter")[7:8] == ["install"], "a hand-written '  DodaKvarter  ' line: Döda Kvarter to install")
        o = run("install", "dodakvarter")
        check(o.strip().splitlines()[-1] == "DONE 0.3.0" and "dodakvarter" not in open(hidden).read().lower(),
              "install takes the hand-written line out")
        # a system without a built-in app's folder and no release of it in the catalog (Bshare): not installed, and
        # not installable
        os.rename(root + "/usr/share/banana/apps/bshare", root + "/usr/share/banana/apps/bshare.away")
        check(row("bshare")[5:8] == ["", "", "unavailable"], "Bshare's folder gone: unavailable: %s" % row("bshare")[5:8])
        o = run("install", "bshare")
        check("FAIL" in o and "bshare" not in open(hidden).read().split(),
              "install without its folder fails: " + o.strip().splitlines()[-1])
        os.rename(root + "/usr/share/banana/apps/bshare.away", root + "/usr/share/banana/apps/bshare")
        # BananaOS 0.5.3 on: Poketool no longer in the system (no folder), its release in the catalog: downloaded into
        # /data/store/apps/bank, on the Apps shelf, started by the system's run-bank with that folder; the bank on the
        # card (/data/apps/bank) is never touched
        os.makedirs(root + "/data/apps/bank")
        with open(root + "/data/apps/bank/bank.json", "w") as f:
            f.write("{}\n")
        z = os.path.join(t, "dl", "bank-v0.2.0", "rocknixds-bank-0.2.0-aarch64.tar.gz")
        os.makedirs(os.path.dirname(z))
        with tarfile.open(z, "w:gz") as tf:
            for name, body, mode in (("rocknixds-bank/rocknixds-bank", b"elf\n", 0o755), ("rocknixds-bank/VERSION", b"0.2.0\n", 0o644),
                                     ("rocknixds-bank/ROCKNIXDS Bank.sh", b"#!/bin/bash\nswaymsg\n", 0o755),
                                     ("rocknixds-bank/media/bank-image.png", open(os.path.join(HERE, "apps/bank/icon.png"), "rb").read(), 0o644)):
                ti = tarfile.TarInfo(name)
                ti.size, ti.mode = len(body), mode
                tf.addfile(ti, io.BytesIO(body))
        api = os.path.join(t, "api", "JorreFog", "ROCKNIXDS.json")
        os.makedirs(os.path.dirname(api), exist_ok=True)
        with open(api, "w") as f:
            json.dump([{"tag_name": "bank-v0.2.0", "prerelease": True, "body": "Poketool", "assets": [
                {"name": os.path.basename(z), "size": os.path.getsize(z),
                 "digest": "sha256:" + hashlib.sha256(open(z, "rb").read()).hexdigest()}]}], f)
        hidden_before = open(hidden).read()
        os.rename(root + "/usr/share/banana/apps/bank", root + "/usr/share/banana/apps/bank.away")
        run("refresh")
        check(row("bank")[5:8] == ["", "0.2.0", "install"], "no Poketool in the system: 0.2.0 to install: %s" % row("bank")[5:8])
        o = run("install", "bank")
        check("FAIL Needs a newer BananaOS" in o and not os.path.exists(root + "/data/store/tiles/Poketool.sh"),
              "a system without run-bank refuses it: " + o.strip().splitlines()[-1])
        os.makedirs(root + "/usr/libexec/banana", exist_ok=True)
        with open(root + "/usr/libexec/banana/run-bank", "w") as f:
            f.write("#!/bin/sh\n")
        o = run("install", "bank")
        check(o.strip().splitlines()[-1] == "DONE 0.2.0", "install Poketool: " + o.strip().splitlines()[-1])
        bk = root + "/data/store/apps/bank"
        check(os.path.isfile(bk + "/rocknixds-bank") and os.access(bk + "/rocknixds-bank", os.X_OK), "the app in /data/store/apps/bank")
        entry = root + "/data/store/tiles/Poketool.sh"
        text = open(entry).read() if os.path.exists(entry) else ""
        check("exec /usr/libexec/banana/run-bank '/data/store/apps/bank' \"$@\"" in text and "swaymsg" not in text,
              "its Apps shelf entry runs the system's run-bank with its folder: " + text.strip().splitlines()[-1:][0] if text else "no entry")
        gl = open(root + "/data/store/tiles/gamelist.xml").read()
        check("<path>./Poketool.sh</path>" in gl and "<image>./images/bank-image.png</image>" in gl
              and os.path.isfile(root + "/data/store/tiles/images/bank-image.png"), "its line and picture on the shelf")
        check(open(hidden).read() == hidden_before, "hidden-systems left alone")
        check(row("bank")[5:8] == ["0.2.0", "0.2.0", "installed"], "list: Poketool 0.2.0 installed: %s" % row("bank")[5:8])
        o = run("remove", "bank")
        check(o.strip().endswith("DONE") and not os.path.exists(entry) and not os.path.exists(bk), "remove Poketool: entry and app gone")
        check(open(root + "/data/apps/bank/bank.json").read() == "{}\n", "the bank on the card untouched")
        os.rename(root + "/usr/share/banana/apps/bank.away", root + "/usr/share/banana/apps/bank")
        check(row("bank")[7:8] == ["installed"], "with its folder back in the system: the built-in one again")
        # the Store's own tile and the Apps shelf are never taken off the menu, whatever a catalog says
        cat = json.load(open(os.path.join(share, "catalog.json")))
        cat["apps"].append({"id": "shelf", "name": "Apps", "builtin": {"system": "storeapps"}})
        with open(os.path.join(share, "catalog.json"), "w") as f:
            json.dump(cat, f)
        run("refresh")
        run("remove", "shelf")
        check("storeapps" not in open(hidden).read().lower(), "a builtin entry for the Apps shelf can't hide it")
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
