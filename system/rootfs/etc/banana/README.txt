(This file comes with BananaOS and every update replaces it: keep notes of your own in another file.)
BANANA
======
Games go in roms/<system>: roms/nds for Nintendo DS (.nds or .zip), roms/gba, roms/snes, ...
BIOS files go in bios/.
Box art (optional): roms/<system>/media/box/<game file name>.png  (also media/cart, media/label),
or an EmulationStation gamelist.xml with its images, as scrapers write them.

Wi-Fi: Settings (Start) > Network lists the networks around and joins one, the password typed on the
keyboard there. Or put your network in banana/wifi.conf on a computer:
    network={
        ssid="My network"
        psk="password"
    }

Files (a tile on the home screen): copy, move, rename and delete files and folders on this card and the second
SD card, and make folders. Y marks, X says what can be done. The card's own folders stay where they are.
A copy keeps Files open till it is done; Y twice stops it.
Files also gets games from a NAS or a computer over SMB, SFTP or FTP (Add a server in its list), into the folder
their type says or any folder you choose, and unpacks zip, 7z and RAR archives (X, Unpack... on one on the card).
Its servers are kept in banana/nas.conf (a remembered password sealed for this handheld: another one, or a
computer, can't open it), its downloads in banana/downloads, its log in banana/logs/downloads.log. Download
options there: banana/settings.conf's files_* lines.

File sharing: Settings > Network > File sharing puts this card on the network (\\<address>\banana in Windows,
smb://<address>/banana on a Mac), so games can be copied in over Wi-Fi. It asks for a name and a password:
name banana, password linux, until you change them in Settings > Network > File sharing > Sharing name and password (Sharing
login > Guest: no password, only on a network you trust). The second SD card, while it's in, is \\<address>\sd2.
DS saves from ROCKNIX beside the games (roms/nds/<game>.dsv) are taken when a game starts here and BananaOS has no save
for it yet (so a save you delete here comes back from that file; delete or rename the .dsv too).

Bluetooth: Settings > Network > Bluetooth switches it on (off at first: it costs battery), Bluetooth devices
finds controllers, headphones and speakers in pairing mode and pairs them (A). While Bluetooth headphones are
connected the sound plays on them and the volume keys change their volume. Pairings: banana/bluetooth.tar.

Speaker EQ: Settings > Display & sound > Speaker EQ evens out the speakers (never the headphones): a profile
measured for this handheld, or one of your own, and seven sliders to adjust it by ear. Your own: a .txt in banana/speaker-eq in
Equalizer APO's parametric format (what AutoEq and REW export: "Preamp: -3 dB", "Filter 1: ON PK Fc 1000 Hz Gain -4 dB
Q 1.2"), picked in Profile.

SSH (for working on Banana itself): put your public key in banana/ssh/authorized_keys, switch on
Settings > Network > File sharing > SSH, then ssh root@<address>. Keys only, no passwords.

RetroArch (the systems other than the DS): F + a button in a game, F + X its menu, F + Start quits.
A button bound away in RetroArch's
menu (an A that stopped working): Settings > Other systems > Reset RetroArch puts its settings back to Banana's,
the old ones moved to retroarch/backup-<date>; saves, states and the RetroAchievements account stay.

Game & Watch (MAME): the games' MAME ROM zips in roms/gameandwatch (gnw_dkong2.zip...), and their MAME artwork zips,
as you downloaded them, in roms/gameandwatch/artwork (the same names: gnw_dkong2.zip). On the second SD card the same
folders. Without artwork a game still plays, its plain LCD. Names and covers fill in by themselves (the covers drawn
from the artwork). Start = Game A, Select = Game B, L = Time, R = Alarm; F opens the menu (views, controls, Quit),
F + Start quits, L2 swaps the panels, F + L2 saves a screenshot (in screenshots). From a RomM server: its Game & Watch
platform, the artwork in its artwork folder (not its covers: "covers =" under [gameandwatch] in banana/systems.conf
brings them instead of the drawn ones).

Settings: banana/settings.conf (menu), banana/bananad.conf (sleep, power), banana/systems.conf (the systems
list, to add or change emulators: only what you change, e.g. "[psx]" and a "run = ..." line under it; it is merged over
the built-in list, banana/systems.conf.default, so the rest keeps up with updates).
A DS game's own emulator and shader (X on the game in the library): banana/nds-per-game.conf and
banana/nds-per-game-shader.conf, one "<game's file name> = <emulator or shader>" a line (none: no shader); a game
not in them follows Settings > Nintendo DS. Its other rows the same way: nds-per-game-res.conf (BananaDS's 3D
resolution), nds-per-game-timing.conf (BananaDS's timing: accurate or fast), nds-per-game-3d.conf (DraStic's
Threaded 3D), nds-per-game-mic.conf (the microphone).
A game's own name and sort title (X on any game in the library): banana/game-titles.tsv, one
"<system>/<game's file> TAB name TAB sort title" a line; scraping box art again leaves it alone. A gamelist.xml's
<sortname> sorts the game too.
Logs: banana/logs/.
