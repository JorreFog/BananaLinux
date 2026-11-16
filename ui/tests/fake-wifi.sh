#!/bin/sh
# A stand-in for /usr/libexec/banana/wifi in the headless test: networks around, and "connected" to the network
# written first in $FAKE_WIFI_CONF (the settings screen puts the one it joins there).
# FAKE_WIFI_MOVE=<file>: three networks that change places between scans instead (Bravo's signal drops after the second
# scan, counted in the file), none of them connected: for the cursor that must stay on its network
if [ -n "$FAKE_WIFI_MOVE" ]; then
    case $1 in
    scan)
        n=$(cat "$FAKE_WIFI_MOVE" 2>/dev/null); n=$((${n:-0} + 1)); echo $n > "$FAKE_WIFI_MOVE"
        bravo=-50; [ $n -gt 2 ] && bravo=-70
        printf 'bssid / frequency / signal level / flags / ssid\n'
        printf '02:00:00:00:00:01\t2437\t-40\t[WPA2-PSK-CCMP][ESS]\tAlpha\n'
        printf '02:00:00:00:00:02\t2437\t%s\t[WPA2-PSK-CCMP][ESS]\tBravo\n' $bravo
        printf '02:00:00:00:00:03\t2437\t-60\t[WPA2-PSK-CCMP][ESS]\tCharlie\n' ;;
    status) printf 'wpa_state=SCANNING\n' ;;
    esac
    exit 0
fi
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
test)
    # the setup's check: FAKE_WIFI_NET / FAKE_WIFI_TIME = fail for a network with no internet / no time server
    ssid=$(sed -n 's/^[[:space:]]*ssid="\(.*\)"$/\1/p' "$FAKE_WIFI_CONF" 2>/dev/null | head -n 1)
    [ -n "$ssid" ] || { echo "link fail"; exit 0; }
    echo "link ok 192.168.1.23"
    echo "internet ${FAKE_WIFI_NET:-ok}"
    echo "time ${FAKE_WIFI_TIME:-ok}"
    ;;
restart|up|down) ;;
esac
