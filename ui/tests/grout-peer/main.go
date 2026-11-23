// Grout's own save sync, run headless on a test card, so romm-interop.sh can play its table against the real Grout
// code as well as against the scripted one in romm-peers.py. Built only inside a Grout checkout, at the commit in
// ui/tests/GROUT, through `go build -overlay` (tools/grout-peer.sh), so Grout's tree is never written to:
//
//	grout-peer <url> <token> <dir> [<name>]
//
// <dir> is the card: Grout as on NextUI (CFW=NEXTUI, BASE_PATH=<dir>), its GBA ROMs in "Roms/Game Boy Advance (GBA)"
// and its saves in Saves/GBA. What Grout keeps between runs (config.json with the device it registered, its cache
// with the slot and hash it recorded per save, its log) is in <dir>/.grout, its working directory, where the app
// keeps them beside itself. Each run does what the app's sync does: registers once (RegisterDevice, which sends the
// version again), fills the cache from the server's GBA platform as the app does at start, then ResolveSaveSync and
// ExecuteSaveSync. A conflict stays one, as it does when nobody answers Grout's screen. It prints the same JSON
// summary as the scripted Grout: uploaded, downloaded, conflicts, skipped, failed (save file names) and the session;
// negotiation's operations are in Grout's own log, .grout/logs, as the app writes them at debug level.
package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"grout/cache"
	"grout/catalog"
	"grout/romm"
	"grout/saves"
	"grout/settings"

	gaba "github.com/BrandonKowalski/gabagool/v2/pkg/gabagool"
	"go.uber.org/atomic"
)

func fail(f string, a ...any) {
	fmt.Fprintf(os.Stderr, "grout-peer: "+f+"\n", a...)
	os.Exit(1)
}

func main() {
	if len(os.Args) < 4 {
		fail("usage: grout-peer <url> <token> <dir> [<name>]")
	}
	url, token := os.Args[1], os.Args[2]
	dir, err := filepath.Abs(os.Args[3])
	if err != nil {
		fail("%v", err)
	}
	name := "grout-" + filepath.Base(dir)
	if len(os.Args) > 4 {
		name = os.Args[4]
	}
	os.Setenv("CFW", "NEXTUI")
	os.Setenv("BASE_PATH", dir)
	state := filepath.Join(dir, ".grout")
	if err := os.MkdirAll(state, 0o755); err != nil {
		fail("%v", err)
	}
	if err := os.Chdir(state); err != nil {
		fail("%v", err)
	}
	// Grout's logger writes to stdout as well as logs/app.log; stdout is the summary's alone, so the log goes only to
	// the file (the logger takes os.Stdout when first asked for, which is below)
	summaryOut := os.Stdout
	if os.Stdout, err = os.OpenFile(os.DevNull, os.O_WRONLY, 0); err != nil {
		fail("%v", err)
	}
	gaba.SetRawLogLevel("debug")

	// The first run writes the config Grout's setup would: one host, the token, and GBA mapped to NextUI's folder
	if _, err := os.Stat(settings.ConfigFileName); errors.Is(err, os.ErrNotExist) {
		first := &settings.Config{
			Hosts:             []settings.Host{{RootURI: url, Token: token}},
			DirectoryMappings: map[string]settings.DirectoryMapping{"gba": {RomMSlug: "gba", RelativePath: "Game Boy Advance (GBA)"}},
		}
		if err := settings.SaveConfig(first); err != nil {
			fail("%v", err)
		}
	}
	config, err := settings.LoadConfig()
	if err != nil {
		fail("%v", err)
	}
	host := &config.Hosts[0]
	client := romm.NewClientFromHost(*host, config.ApiTimeout.Duration())
	if host.DeviceID == "" {
		dev, err := saves.RegisterDevice(client, name)
		if err != nil || dev.ID == "" {
			fail("registering: %v", err)
		}
		host.DeviceID, host.DeviceName = dev.ID, dev.Name
		if err := settings.SaveConfig(config); err != nil {
			fail("%v", err)
		}
	}

	if err := cache.InitCacheManager(*host, *config); err != nil {
		fail("cache: %v", err)
	}
	cm := cache.GetCacheManager()
	defer cm.Close()
	platforms, err := catalog.MappedPlatforms(*host, config.DirectoryMappings, config.ApiTimeout.Duration())
	if err != nil {
		fail("platforms: %v", err)
	}
	if _, err := cm.PopulateFullCacheWithProgress(platforms, &atomic.Float64{}); err != nil {
		fail("filling the cache: %v", err)
	}

	res, err := saves.ResolveSaveSync(client, config, host.DeviceID)
	if err != nil {
		fail("%v", err)
	}
	report := saves.ExecuteSaveSync(client, config, host.DeviceID, res.Items, res.SessionID, nil)

	summary := map[string]any{"uploaded": []string{}, "downloaded": []string{}, "conflicts": []string{}, "skipped": []string{},
		"failed": []string{}, "session": res.SessionID}
	add := func(k, v string) { summary[k] = append(summary[k].([]string), v) }
	for _, it := range report.Items {
		f := it.LocalSave.FileName
		if it.LocalSave.FilePath != "" {
			f = filepath.Base(it.LocalSave.FilePath)
		}
		if f == "" && it.RemoteSave != nil {
			f = it.RemoteSave.FileName
		}
		switch {
		case it.Action == saves.ActionConflict:
			add("conflicts", f)
		case it.Action == saves.ActionSkip:
			add("skipped", f)
		case !it.Success:
			add("failed", f)
		case it.Action == saves.ActionUpload:
			add("uploaded", f)
		case it.Action == saves.ActionDownload:
			add("downloaded", f)
		}
	}
	out, _ := json.Marshal(summary)
	fmt.Fprintln(summaryOut, string(out))
}
