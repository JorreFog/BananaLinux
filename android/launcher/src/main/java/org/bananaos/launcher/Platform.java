package org.bananaos.launcher;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.display.DisplayManager;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;

import java.io.File;
import java.lang.reflect.Method;
import java.net.Inet4Address;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The Java half of the platform (ui/src/android_platform.h): what the core's shell tools did on BananaOS's image, done
 * with Android's own services. The core calls call() with a tool's words and gets "<exit status>\n<output>" back, the
 * output in the tool's own format (wpa_cli's for Wi-Fi, the bluetooth tool's tab-separated devices...).
 *
 * It also keeps the sysfs files the core reads mirrored under <files>/root/sys (the battery, Wi-Fi's link, the
 * backlights), since an app can't read the real ones.
 */
@SuppressLint("MissingPermission")
public final class Platform {
    private static Context app;
    private static File sys;

    static void init(Context c) {
        app = c;
        sys = new File(Core.root, "sys/class");
        Core.write(new File(Core.root, "proc/device-tree/model"), "AYN Thor");
        for (String s : new String[] { "top", "bottom" }) {
            Core.write(new File(sys, "backlight/" + s + "/max_brightness"), "1000\n");
            File b = new File(sys, "backlight/" + s + "/brightness");
            if (!b.exists()) Core.write(b, "500\n");
        }
        Core.write(new File(sys, "power_supply/battery/type"), "Battery\n");
        Core.write(new File(sys, "power_supply/usb/type"), "USB\n");
        IntentFilter f = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        Intent sticky = c.registerReceiver(battery, f);
        if (sticky != null) battery.onReceive(c, sticky);
        c.getSystemService(ConnectivityManager.class).registerDefaultNetworkCallback(new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network n) { link(true); }
            @Override public void onLost(Network n) { link(false); }
        });
        link(false);
        // (the daemon stops the fan and the lights for a sleep, PROTOCOL.md: Android suspends by itself)
        IntentFilter sc = new IntentFilter(Intent.ACTION_SCREEN_OFF);
        sc.addAction(Intent.ACTION_SCREEN_ON);
        c.registerReceiver(new BroadcastReceiver() {
            @Override public void onReceive(Context x, Intent i) {
                String req = Intent.ACTION_SCREEN_OFF.equals(i.getAction()) ? "SUSPEND" : "RESUME";
                new Thread(() -> Hwd.request(req)).start();
            }
        }, sc);
    }

    private static final BroadcastReceiver battery = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            int level = i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), scale = i.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            int status = i.getIntExtra(BatteryManager.EXTRA_STATUS, 0), plugged = i.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
            if (level >= 0 && scale > 0) Core.write(new File(sys, "power_supply/battery/capacity"), (level * 100 / scale) + "\n");
            String st = status == BatteryManager.BATTERY_STATUS_CHARGING ? "Charging" : status == BatteryManager.BATTERY_STATUS_FULL ? "Full"
                : status == BatteryManager.BATTERY_STATUS_NOT_CHARGING ? "Not charging" : "Discharging";
            Core.write(new File(sys, "power_supply/battery/status"), st + "\n");
            Core.write(new File(sys, "power_supply/usb/online"), (plugged != 0 ? "1" : "0") + "\n");
        }
    };

    private static void link(boolean up) {
        WifiManager wm = app.getSystemService(WifiManager.class);
        boolean wifi = up || (wm != null && wm.isWifiEnabled() && wm.getConnectionInfo() != null && wm.getConnectionInfo().getNetworkId() >= 0);
        Core.write(new File(sys, "net/wlan0/operstate"), wifi ? "up\n" : "down\n");
    }

    /** a tool's words (the tool's name first): "<status>\n<output>" */
    public static String call(String[] a) {
        try {
            switch (a[0]) {
                case "volume": return volume(a);
                case "brightness": return brightness(a);
                case "wifi": return wifi(a);
                case "bluetooth": return bluetooth(a);
                case "timezone": return timezone(a);
            }
            return "127\n";
        } catch (Exception e) {
            Log.w(Core.TAG, "platform " + String.join(" ", a), e);
            return "1\n";
        }
    }

    private static String ok(String out) { return "0\n" + out; }

    // ---- volume: the media stream's level in percent ----
    private static String volume(String[] a) {
        AudioManager am = app.getSystemService(AudioManager.class);
        int max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        if (a[1].equals("get")) return ok((am.getStreamVolume(AudioManager.STREAM_MUSIC) * 100 + max / 2) / max + "\n");
        if (a[1].equals("set")) { am.setStreamVolume(AudioManager.STREAM_MUSIC, Math.round(Integer.parseInt(a[2]) * max / 100f), 0); return ok(""); }
        return "2\n";
    }

    // ---- brightness without banana-hwd: DisplayManager's per display (a system API: the platform-signed launcher
    // has CONTROL_DISPLAY_BRIGHTNESS), else the top screen's Settings value ----
    private static String brightness(String[] a) {
        int screen = Integer.parseInt(a[2]);
        DisplayManager dm = app.getSystemService(DisplayManager.class);
        Display d = screen == 0 ? dm.getDisplay(Display.DEFAULT_DISPLAY) : Core.bottomDisplay(dm);
        if (d == null) return "1\n";
        if (a[1].equals("set")) {
            float f = Math.max(0.01f, Math.min(1f, Integer.parseInt(a[3]) / 1000f));
            try {
                Method m = DisplayManager.class.getMethod("setBrightness", int.class, float.class);
                m.invoke(dm, d.getDisplayId(), f);
                return ok("");
            } catch (Exception e) {
                Log.w(Core.TAG, "DisplayManager.setBrightness: " + e);
            }
            if (screen != 0) return "1\n";
            Settings.System.putInt(app.getContentResolver(), Settings.System.SCREEN_BRIGHTNESS_MODE, Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL);
            Settings.System.putInt(app.getContentResolver(), Settings.System.SCREEN_BRIGHTNESS, Math.round(f * 255));
            return ok("");
        }
        try {
            Method m = DisplayManager.class.getMethod("getBrightness", int.class);
            return ok(Math.round((float) m.invoke(dm, d.getDisplayId()) * 1000) + "\n");
        } catch (Exception e) {
            if (screen != 0) return "1\n";
            int v = Settings.System.getInt(app.getContentResolver(), Settings.System.SCREEN_BRIGHTNESS, 128);
            return ok(v * 1000 / 255 + "\n");
        }
    }

    // ---- Wi-Fi, in wpa_cli's words (ui/src/wifi.cpp reads them) ----
    @SuppressWarnings("deprecation")
    private static String wifi(String[] a) throws Exception {
        WifiManager wm = app.getSystemService(WifiManager.class);
        switch (a[1]) {
            case "up": wm.setWifiEnabled(true); return ok("");
            case "down": wm.setWifiEnabled(false); return ok("");
            case "status": {
                WifiInfo w = wm.getConnectionInfo();
                if (w == null || w.getNetworkId() < 0) return ok("wpa_state=" + (wm.isWifiEnabled() ? "SCANNING" : "INTERFACE_DISABLED") + "\n");
                String ssid = w.getSSID();
                if (ssid.startsWith("\"") && ssid.endsWith("\"")) ssid = ssid.substring(1, ssid.length() - 1);
                return ok("wpa_state=COMPLETED\nssid=" + ssid + "\nip_address=" + ip() + "\n");
            }
            case "scan": {
                if (!wm.isWifiEnabled()) wm.setWifiEnabled(true);
                wm.startScan();
                Thread.sleep(3000);
                StringBuilder o = new StringBuilder("bssid / frequency / signal level / flags / ssid\n");
                List<ScanResult> rs = wm.getScanResults();
                for (ScanResult r : rs) o.append(r.BSSID).append('\t').append(r.frequency).append('\t').append(r.level).append('\t')
                    .append(r.capabilities).append('\t').append(r.SSID).append('\n');
                return ok(o.toString());
            }
            case "restart": return join(wm);
            case "test": {
                ConnectivityManager cm = app.getSystemService(ConnectivityManager.class);
                NetworkCapabilities nc = cm.getNetworkCapabilities(cm.getActiveNetwork());
                String ipa = ip();
                boolean net = nc != null && nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
                return ok((ipa.isEmpty() ? "link fail\n" : "link ok " + ipa + "\n") + (net ? "internet ok\ntime ok\n" : "internet fail\ntime fail\n"));
            }
        }
        return "2\n";
    }

    private static String ip() {
        ConnectivityManager cm = app.getSystemService(ConnectivityManager.class);
        LinkProperties lp = cm.getLinkProperties(cm.getActiveNetwork());
        if (lp != null) for (LinkAddress la : lp.getLinkAddresses()) if (la.getAddress() instanceof Inet4Address) return la.getAddress().getHostAddress();
        return "";
    }

    /** the network Settings put first in banana/wifi.conf joined: the system's Wi-Fi as a privileged app may (the
     * platform-signed launcher), else the system's Wi-Fi panel to pick it there */
    @SuppressWarnings("deprecation")
    private static String join(WifiManager wm) {
        String conf = Core.read(new File(Core.data, "wifi.conf"));
        Matcher m = conf == null ? null : Pattern.compile("network=\\{(.*?)\\}", Pattern.DOTALL).matcher(conf);
        if (m != null && m.find()) {
            String block = m.group(1);
            Matcher s = Pattern.compile("ssid=\"(.*)\"").matcher(block), p = Pattern.compile("psk=\"(.*)\"").matcher(block);
            if (s.find()) {
                WifiConfiguration c = new WifiConfiguration();
                c.SSID = "\"" + s.group(1) + "\"";
                if (p.find()) c.preSharedKey = "\"" + p.group(1) + "\"";
                else c.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
                int id = wm.addNetwork(c);
                if (id >= 0 && wm.enableNetwork(id, true)) return ok("");
            }
        }
        Core.ui.post(() -> app.startActivity(new Intent(Settings.Panel.ACTION_WIFI).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)));
        return ok("");
    }

    // ---- Bluetooth, in the bluetooth tool's words (ui/src/bluetooth.cpp) ----
    @SuppressWarnings("deprecation")
    private static String bluetooth(String[] a) {
        BluetoothAdapter ad = app.getSystemService(BluetoothManager.class).getAdapter();
        if (ad == null) return "1\n";
        switch (a[1]) {
            case "status": return ok(ad.isEnabled() ? "on\n" : "off\n");
            case "up": ad.enable(); return ok("");
            case "down": ad.disable(); return ok("");
            case "devices": case "scan": {
                if (a[1].equals("scan")) {
                    // TODO(android): a search and pairing of our own (BluetoothDevice.createBond): the system's
                    // Bluetooth page does it for now
                    Core.ui.post(() -> app.startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)));
                }
                StringBuilder o = new StringBuilder();
                for (BluetoothDevice d : ad.getBondedDevices()) {
                    BluetoothClass c = d.getBluetoothClass();
                    int major = c == null ? 0 : c.getMajorDeviceClass();
                    String kind = major == BluetoothClass.Device.Major.AUDIO_VIDEO ? "audio" : major == BluetoothClass.Device.Major.PERIPHERAL
                        ? (c.getDeviceClass() & 0xC0) == 0x40 ? "keyboard" : (c.getDeviceClass() & 0xC0) == 0x80 ? "mouse" : "gamepad" : "other";
                    boolean connected = false;
                    try { connected = (boolean) BluetoothDevice.class.getMethod("isConnected").invoke(d); } catch (Exception ignored) { }
                    o.append(d.getAddress()).append("\t1\t").append(connected ? "1" : "0").append('\t').append(kind).append('\t')
                        .append(d.getName() == null ? "" : d.getName()).append('\n');
                }
                return ok(o.toString());
            }
            default:
                // TODO(android): pair, connect, disconnect, forget, audio: the system's Bluetooth page
                Core.ui.post(() -> app.startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)));
                return ok("OK\n");
        }
    }

    // ---- the time zone: settings.conf keeps a POSIX rule; Android wants a zone's name ----
    private static String timezone(String[] a) {
        // TODO(android): the POSIX rule (CET-1CEST,M3.5.0,M10.5.0/3) to an Olson name for AlarmManager.setTimeZone
        // (SET_TIME_ZONE): the core's own clock follows TZ already
        if (a.length > 1 && a[1].contains("/")) {
            app.getSystemService(android.app.AlarmManager.class).setTimeZone(a[1]);
            return ok("");
        }
        return "1\n";
    }

    private Platform() { }
}
