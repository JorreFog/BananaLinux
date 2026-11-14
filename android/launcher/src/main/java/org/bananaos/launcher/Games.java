package org.bananaos.launcher;

import android.app.ActivityManager;
import android.app.ActivityOptions;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;
import android.view.Display;
import android.widget.Toast;

import java.io.File;
import java.lang.reflect.Field;
import java.util.List;

/**
 * Games as other apps. The core finds a system's entry in the launch table (ui/src/android_launch.h) and hands it here
 * as "key=value" lines; this makes the Intent and starts it on the top display. The game's task stays behind the
 * launcher when the Home button brings the menu forward, and is brought back to the front by the next tap (or by picking
 * the same game again).
 */
final class Games {
    private static String pkg, rom;
    private static long started;
    private static boolean told;	// (the launcher back over this game told to the core once: its play time)

    static boolean running() { return pkg != null; }

    /** null when started, else why not (shown to the player too) */
    static String launch(Context c, String[] spec) {
        String system = "", action = Intent.ACTION_VIEW, data = "none", mime = null, activity = null, flags = "", romPath = "", error = null;
        String p = null;
        Intent i = new Intent();
        for (String kv : spec) {
            int eq = kv.indexOf('=');
            if (eq < 0) continue;
            String k = kv.substring(0, eq), v = kv.substring(eq + 1);
            switch (k) {
                case "error": error = v; break;
                case "system": system = v; break;
                case "package": p = v; break;
                case "activity": activity = v; break;
                case "action": action = v; break;
                case "data": data = v; break;
                case "mime": mime = v; break;
                case "flags": flags = v; break;
                case "rom": romPath = v; break;
                default:
                    if (k.startsWith("extra.")) i.putExtra(k.substring(6), v.replace("%ROMURI%", RomProvider.uriFor(c, romPath).toString()));
            }
        }
        if (error != null) { Toast.makeText(c, error, Toast.LENGTH_LONG).show(); return error; }
        if (p == null) return "no package";
        // (launch.conf's package = banana:browser: the web browser is Android's own here)
        if (p.equals("banana:browser")) {
            try {
                c.startActivity(Intent.makeMainSelectorActivity(Intent.ACTION_MAIN, Intent.CATEGORY_APP_BROWSER).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK), onTop().toBundle());
                return null;
            } catch (Exception e) { return "no web browser: " + e.getMessage(); }
        }
        // the same game again, still running behind the menu: back into it, not started over
        if (p.equals(pkg) && romPath.equals(rom)) { backToGame(c); return null; }
        i.setAction(action);
        if (activity != null) i.setComponent(new ComponentName(p, activity.startsWith(".") ? p + activity : activity));
        else i.setPackage(p);
        Uri u = null;
        Uri romUri = RomProvider.uriFor(c, romPath);
        if (data.equals("file")) u = Uri.fromFile(new File(romPath));
        else if (data.equals("content")) u = romUri;
        // (a URI template, its placeholders filled in by the core but %ROMURI%: scummvm:<id>, gamenative://run?appid=...)
        else if (!data.equals("none") && !data.isEmpty()) u = Uri.parse(data.replace("%ROMURI%", romUri.toString()));
        boolean grant = data.equals("content") || data.contains("%ROMURI%");
        for (String kv : spec) grant |= kv.startsWith("extra.") && kv.contains("%ROMURI%");
        if (grant) {
            // (the emulator reads the game through the launcher's provider: the grant on the intent, its ClipData too)
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            i.setClipData(android.content.ClipData.newRawUri("", romUri));
            c.grantUriPermission(p, romUri, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        }
        if (u != null && mime != null) i.setDataAndType(u, mime);
        else if (u != null) i.setData(u);
        else if (mime != null) i.setType(mime);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        for (String f : flags.trim().split("\\s+")) {
            if (f.isEmpty()) continue;
            try {
                Field fl = Intent.class.getField(f);
                i.addFlags(fl.getInt(null));
            } catch (Exception e) { Log.w(Core.TAG, "launch.conf: no Intent flag " + f); }
        }
        try {
            c.startActivity(i, onTop().toBundle());
        } catch (Exception e) {
            String why = "The emulator for " + system + " (" + p + ") didn't start: " + e.getMessage();
            Toast.makeText(c, why, Toast.LENGTH_LONG).show();
            return why;
        }
        pkg = p;
        rom = romPath;
        started = System.currentTimeMillis();
        told = false;
        Log.i(Core.TAG, "game: " + i);
        return null;
    }

    private static ActivityOptions onTop() { return ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY); }

    /** the running game's task to the front again (REAL_GET_TASKS lets a privileged app see it; else its launch intent) */
    @SuppressWarnings("deprecation")
    static void backToGame(Context c) {
        if (pkg == null) return;
        ActivityManager am = c.getSystemService(ActivityManager.class);
        try {
            List<ActivityManager.RunningTaskInfo> tasks = am.getRunningTasks(16);
            for (ActivityManager.RunningTaskInfo t : tasks)
                if (t.baseActivity != null && pkg.equals(t.baseActivity.getPackageName())) {
                    am.moveTaskToFront(t.taskId, 0, onTop().toBundle());
                    return;
                }
        } catch (SecurityException e) { Log.w(Core.TAG, "no task list: " + e.getMessage()); }
        Intent i = c.getPackageManager().getLaunchIntentForPackage(pkg);
        if (i == null) { pkg = null; return; }	// (gone: uninstalled)
        // (an existing task with the package's affinity comes forward as it was, with no new intent)
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        try { c.startActivity(i, onTop().toBundle()); } catch (Exception e) { Log.w(Core.TAG, "back to the game", e); }
    }

    /** the menu over whatever is in front (the Home button's tap over a game) */
    static void toLauncher(Context c) {
        Intent i = new Intent(c, HomeActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        try { c.startActivity(i, ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY).toBundle()); }
        catch (Exception e) { Log.w(Core.TAG, "to the launcher", e); }
    }

    /** the launcher came back to the front: the game's play time so far, once a start (the core records it) */
    static void launcherBack() {
        if (pkg == null || told) return;
        told = true;
        Core.nativeGameBack(rom, (System.currentTimeMillis() - started) / 1000);
    }

    private Games() { }
}
