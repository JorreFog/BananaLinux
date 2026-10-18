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

File sharing: Settings > File sharing puts this card on the network (\\<address>\banana in Windows,
smb://<address>/banana on a Mac), so games can be copied in over Wi-Fi. No password: leave it off on networks
you don't trust.

SSH (for working on Banana itself): put your public key in banana/ssh/authorized_keys, switch on
Settings > SSH, then ssh root@<address>. Keys only, no passwords.

Settings: banana/settings.conf (menu), banana/bananad.conf (sleep, power), banana/systems.conf (the systems
list, to add or change emulators: copy banana/systems.conf.default to it and change the copy).
A DS game's own emulator and shader (X on the game in the library): banana/nds-per-game.conf and
banana/nds-per-game-shader.conf, one "<game's file name> = <emulator or shader>" a line (none: no shader); a game
not in them follows Settings > Nintendo DS.
Logs: banana/logs/.
