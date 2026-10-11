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

Settings: banana/settings.conf (menu), banana/bananad.conf (sleep, power), banana/systems.conf (a copy of
the systems list, to add or change emulators).
Logs: banana/logs/.
