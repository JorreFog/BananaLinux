#!/bin/sh
# A stand-in for /usr/libexec/banana/wifi in the headless test: networks around, and "connected" to the network
# written first in $FAKE_WIFI_CONF (the settings screen puts the one it joins there).
case $1 in
scan)
    printf 'bssid / frequency / signal level / flags / ssid\n'
    printf '02:00:00:00:00:01\t2437\t-48\t[WPA2-PSK-CCMP][ESS]\tBanana Home\n'
    printf '02:00:00:00:00:02\t5180\t-60\t[WPA2-PSK-CCMP][ESS]\tBanana Home\n'
    printf '02:00:00:00:00:03\t2412\t-61\t[WPA2-EAP-CCMP][ESS]\tOffice\n'
    printf '02:00:00:00:00:04\t2462\t-70\t[ESS]\tCafe Free\n'
    printf '02:00:00:00:00:05\t2412\t-74\t[WPA2-PSK+SAE-CCMP][ESS]\tCaf\\xc3\\xa9 \\"Corner\\"\n'
    printf '02:00:00:00:00:06\t5745\t-82\t[RSN-SAE-CCMP][ESS]\tNeighbour 5G\n'
    printf '02:00:00:00:00:07\t2412\t-50\t[WPA2-PSK-CCMP][ESS]\t\\x00\\x00\\x00\n'
    ;;
status)
    ssid=$(sed -n 's/^[[:space:]]*ssid="\(.*\)"$/\1/p' "$FAKE_WIFI_CONF" 2>/dev/null | head -n 1)
    if [ -n "$ssid" ]; then
        printf 'bssid=02:00:00:00:00:01\nssid=%s\nwpa_state=COMPLETED\nip_address=192.168.1.23\n' "$ssid"
    else
        printf 'wpa_state=SCANNING\n'
    fi
    ;;
restart|up|down) ;;
esac
