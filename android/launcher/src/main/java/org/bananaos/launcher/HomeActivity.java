package org.bananaos.launcher;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The HOME activity, on display 0: the top screen. It also starts the bottom screen's activity on display 1 (which the
 * system starts by itself too where display 1 shows system decorations: SECONDARY_HOME).
 *
 * The Home intent (the system's HOME key, or the button mapped to it) brings this forward over a running game; when it
 * comes while this is already in front and a game is running behind it, the game goes back to the front instead.
 */
public class HomeActivity extends Activity {
    static HomeActivity current;
    private boolean stopped = true;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Core.start(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(new ScreenView(this, Core.SLOT_TOP, 0));
        immersive();
        startBottom();
        askFilesAccess();
    }

    private void immersive() {
        getWindow().setDecorFitsSystemWindows(false);
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    /** the bottom screen's activity on the second display, if there is one */
    private void startBottom() {
        Display d = Core.bottomDisplay(getSystemService(DisplayManager.class));
        if (d == null) { Log.i(Core.TAG, "one display only: the bottom screen isn't shown"); return; }
        if (BottomActivity.current != null) return;
        try {
            ActivityOptions o = ActivityOptions.makeBasic().setLaunchDisplayId(d.getDisplayId());
            startActivity(new Intent(this, BottomActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK), o.toBundle());
        } catch (Exception e) {
            Log.w(Core.TAG, "the bottom screen's activity didn't start on display " + d.getDisplayId(), e);
        }
    }

    /** a sideloaded APK (the ROM grants it: README.md): the system's page for all-files access, once */
    private void askFilesAccess() {
        if (Environment.isExternalStorageManager()) return;
        if (getSharedPreferences("banana", MODE_PRIVATE).getBoolean("askedFiles", false)) return;
        getSharedPreferences("banana", MODE_PRIVATE).edit().putBoolean("askedFiles", true).apply();
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        } catch (Exception e) { Log.w(Core.TAG, "no all-files access page", e); }
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        // HOME again while the menu was already in front (paused for the intent, never stopped): back to the game
        if (Intent.ACTION_MAIN.equals(i.getAction()) && i.hasCategory(Intent.CATEGORY_HOME) && !stopped && Games.running())
            Games.backToGame(this);
    }

    @Override protected void onStart() { super.onStart(); stopped = false; current = this; }

    @Override
    protected void onResume() {
        super.onResume();
        immersive();
        startBottom();
        Games.launcherBack();
    }

    @Override protected void onStop() { super.onStop(); stopped = true; }

    @Override protected void onDestroy() { if (current == this) current = null; super.onDestroy(); }

    /** the menu is what the player sees on the top screen now */
    static boolean inFront() { return current != null && !current.stopped && current.hasWindowFocus(); }

    @Override public boolean dispatchKeyEvent(KeyEvent e) { return Keys.key(e) || super.dispatchKeyEvent(e); }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) { return Keys.motion(e) || super.dispatchGenericMotionEvent(e); }

    // (BACK is the menu's own key: B)
    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() { }
}
