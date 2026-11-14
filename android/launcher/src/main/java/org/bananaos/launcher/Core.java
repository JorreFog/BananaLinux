package org.bananaos.launcher;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.hardware.display.DisplayManager;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.util.Log;
import android.view.Display;
import android.view.Surface;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * The menu's core (libbanana.so: ui/src, main.cpp and the android_* files) and what it asks of the app. One core per
 * process, started by the Application; it runs until the process ends and draws on whatever surfaces the activities
 * hand it (slot 0 the top screen, 1 the bottom, 2 the quick sheet).
 */
public final class Core {
    static final String TAG = "banana";
    static final int SLOT_TOP = 0, SLOT_BOTTOM = 1, SLOT_SHEET = 2;

    static Context app;
    static final Handler ui = new Handler(Looper.getMainLooper());
    static File root, data, roms;

    static { System.loadLibrary("banana"); }

    static native void nativeStart(String root, String[] env, String[] args);
    static native void nativeSurface(int slot, Surface surface);
    static native void nativeBottomSize(int w, int h);
    static native void nativeKey(boolean down, int evdevCode);
    static native void nativeAxes(int device, float x, float y, float hatX, float hatY);
    static native void nativeTouch(int screen, int action, float nx, float ny, boolean wide);
    static native void nativeHome(int what);
    static native void nativeGameBack(String rom, long secs);
    static native String nativeShell(String cmd);

    private static boolean started;

    static synchronized void start(Context ctx) {
        if (started) return;
        started = true;
        app = ctx.getApplicationContext();
        root = new File(app.getFilesDir(), "root");
        unpack();
        pickStorage();
        Platform.init(app);
        // display 1's size, for a bottom screen whose activity isn't up yet
        DisplayManager dm = app.getSystemService(DisplayManager.class);
        Display bottom = bottomDisplay(dm);
        if (bottom != null) {
            android.graphics.Point p = new android.graphics.Point();
            bottom.getRealSize(p);
            nativeBottomSize(p.x, p.y);
        }
        String share = new File(root, "share/banana").getPath();
        List<String> env = new ArrayList<>();
        env.add("BANANA_DATA=" + data.getPath());
        env.add("BANANA_ROMS=" + roms.getPath());
        env.add("BANANA_BACKLIGHT_TOP=top");
        env.add("BANANA_BACKLIGHT_BOTTOM=bottom");
        env.add("BANANA_HOME_KEY=1");
        env.add("BANANA_ANDROID=1");
        env.add("HOME=" + app.getFilesDir().getPath());
        env.add("TMPDIR=" + app.getCacheDir().getPath());
        String[] args = {
            "--assets", share + "/rnds", "--splash", share + "/splash", "--banana", share + "/banana",
            "--systems", new File(root, "etc/banana/systems.conf").getPath(),
            "--roms", roms.getPath(), "--data", data.getPath(),
        };
        Log.i(TAG, "core: roms " + roms + ", data " + data);
        nativeStart(root.getPath(), env.toArray(new String[0]), args);
        Hwd.listen();
    }

    /** the second display (the Thor's bottom screen), or null */
    static Display bottomDisplay(DisplayManager dm) {
        for (Display d : dm.getDisplays())
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY && (d.getFlags() & Display.FLAG_PRIVATE) == 0) return d;
        return null;
    }

    /** the APK's root/ unpacked to <files>/root once per install or update (run/ and sys/ are the app's own) */
    private static void unpack() {
        long stamp;
        try {
            PackageInfo pi = app.getPackageManager().getPackageInfo(app.getPackageName(), 0);
            stamp = pi.lastUpdateTime;
        } catch (Exception e) { stamp = 0; }
        File mark = new File(root, ".unpacked");
        String want = Long.toString(stamp);
        if (want.equals(read(mark))) return;
        long t0 = System.currentTimeMillis();
        try { copyTree(app.getAssets(), "root", root); } catch (IOException e) { Log.e(TAG, "unpacking the assets", e); }
        write(mark, want);
        Log.i(TAG, "assets unpacked in " + (System.currentTimeMillis() - t0) + " ms");
    }

    private static void copyTree(AssetManager am, String path, File to) throws IOException {
        String[] kids = am.list(path);
        if (kids == null || kids.length == 0) {
            to.getParentFile().mkdirs();
            try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(to)) {
                byte[] b = new byte[65536];
                for (int n; (n = in.read(b)) > 0;) out.write(b, 0, n);
            } catch (IOException e) {
                to.mkdirs();	// (an empty folder lists nothing either)
            }
            return;
        }
        to.mkdirs();
        for (String k : kids) copyTree(am, path + "/" + k, new File(to, k));
    }

    /**
     * Where the games are: roms/ on a removable card (StorageVolume, /storage/<uuid>), else on the shared storage
     * (/sdcard/roms). banana/ beside it holds the menu's settings and data, as on BananaOS's card. All of it needs
     * all-files access (MANAGE_EXTERNAL_STORAGE: the ROM grants it, README.md).
     */
    private static void pickStorage() {
        File base = null;
        StorageManager sm = app.getSystemService(StorageManager.class);
        for (StorageVolume v : sm.getStorageVolumes()) {
            File dir = v.getDirectory();
            if (dir != null && v.isRemovable() && Environment.MEDIA_MOUNTED.equals(v.getState()) && new File(dir, "roms").isDirectory()) { base = dir; break; }
        }
        if (base == null) base = Environment.getExternalStorageDirectory();
        roms = new File(base, "roms");
        data = new File(base, "banana");
        if (!data.mkdirs() && !data.isDirectory()) {
            // (no access to the shared storage yet: the app's own folder, until it is granted)
            data = new File(app.getFilesDir(), "banana");
            data.mkdirs();
        }
        new File(root, "run/banana").mkdirs();
    }

    static String read(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = in.readAllBytes();
            return new String(b).trim();
        } catch (IOException e) { return null; }
    }

    static void write(File f, String s) {
        f.getParentFile().mkdirs();
        try (OutputStream out = new FileOutputStream(f)) { out.write(s.getBytes()); } catch (IOException e) { Log.w(TAG, "writing " + f, e); }
    }

    // ---- called by the core (any of its threads) ------------------------------------------------------------------

    /** a game started from the launch table's entry ("key=value" lines): null, or why it didn't */
    static String launch(String[] spec) {
        final String[] err = { null };
        CountDownLatch done = new CountDownLatch(1);
        ui.post(() -> { try { err[0] = Games.launch(app, spec); } catch (Exception e) { err[0] = e.toString(); } done.countDown(); });
        try { done.await(5, TimeUnit.SECONDS); } catch (InterruptedException ignored) { }
        return err[0];
    }

    /** the quick sheet's overlay on display 1 shown (true once its surface is there) or taken away */
    static boolean showSheet(boolean on) {
        CountDownLatch done = new CountDownLatch(1);
        final boolean[] ok = { false };
        ui.post(() -> {
            if (on) QuickSheet.show(app, () -> { ok[0] = true; done.countDown(); }, () -> done.countDown());
            else QuickSheet.hide(() -> { ok[0] = true; done.countDown(); });
        });
        try { done.await(on ? 2000 : 1000, TimeUnit.MILLISECONDS); } catch (InterruptedException ignored) { }
        return ok[0];
    }

    /** the core ended (a language change asks for a fresh start): the process goes, and the system starts HOME again */
    static void onExit(int code) {
        Log.i(TAG, "core ended (" + code + "): restarting");
        ui.post(() -> Process.killProcess(Process.myPid()));
    }
}
