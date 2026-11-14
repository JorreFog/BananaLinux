package org.bananaos.launcher;

import android.app.Activity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The bottom screen, on display 1 (SECONDARY_HOME). The same core draws it as the top one (slot 1); its touches are
 * the bottom screen's. It stays up while a game runs on the top screen, as the menu's own bottom screen.
 */
public class BottomActivity extends Activity {
    static BottomActivity current;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        current = this;
        Core.start(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(new ScreenView(this, Core.SLOT_BOTTOM, 1));
        getWindow().setDecorFitsSystemWindows(false);
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    @Override protected void onDestroy() { if (current == this) current = null; super.onDestroy(); }

    @Override public boolean dispatchKeyEvent(KeyEvent e) { return Keys.key(e) || super.dispatchKeyEvent(e); }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) { return Keys.motion(e) || super.dispatchGenericMotionEvent(e); }

    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() { }
}
