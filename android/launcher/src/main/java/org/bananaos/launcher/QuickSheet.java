package org.bananaos.launcher;

import android.content.Context;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * The quick menu's sheet over a game: an overlay window covering display 1 (TYPE_APPLICATION_OVERLAY, which needs
 * SYSTEM_ALERT_WINDOW: the ROM signs the launcher with the platform key, README.md). The core draws the sheet into it
 * (slot 2) and slides it up and down itself (sheet.h's curves, ui/src/android_display.cpp: the rows above the sheet are
 * transparent, the game shows through them). While it is up it has the focus, so the pad and the bottom touchscreen
 * are the menu's; the game keeps running on the top screen.
 */
final class QuickSheet {
    private static FrameLayout view;
    private static WindowManager wm;
    private static Runnable onGone;

    static boolean showing() { return view != null; }

    static void show(Context app, Runnable ready, Runnable failed) {
        if (view != null) { ready.run(); return; }
        if (!Settings.canDrawOverlays(app)) { Log.w(Core.TAG, "quick sheet: no overlay permission"); failed.run(); return; }
        Display d = Core.bottomDisplay(app.getSystemService(DisplayManager.class));
        if (d == null) { failed.run(); return; }
        Context c = app.createDisplayContext(d).createWindowContext(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null);
        wm = c.getSystemService(WindowManager.class);
        FrameLayout box = new FrameLayout(c) {
            @Override public boolean dispatchKeyEvent(KeyEvent e) { return Keys.key(e) || super.dispatchKeyEvent(e); }
            @Override public boolean dispatchGenericMotionEvent(MotionEvent e) { return Keys.motion(e) || super.dispatchGenericMotionEvent(e); }
        };
        SurfaceView sv = new SurfaceView(c) {
            @Override public boolean onTouchEvent(MotionEvent e) {
                int w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
                int a = e.getActionMasked() == MotionEvent.ACTION_DOWN ? 0 : e.getActionMasked() == MotionEvent.ACTION_MOVE ? 1
                    : e.getActionMasked() == MotionEvent.ACTION_UP || e.getActionMasked() == MotionEvent.ACTION_CANCEL ? 2 : -1;
                if (a >= 0) Core.nativeTouch(1, a, e.getX(0) / w, e.getY(0) / h, w > h);
                return true;
            }
        };
        sv.setZOrderOnTop(true);
        sv.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        final boolean[] said = { false };
        sv.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder h) { }
            @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int hh) {
                Core.nativeSurface(Core.SLOT_SHEET, h.getSurface());
                if (!said[0]) { said[0] = true; ready.run(); }
            }
            @Override public void surfaceDestroyed(SurfaceHolder h) {
                Core.nativeSurface(Core.SLOT_SHEET, null);
                if (onGone != null) { onGone.run(); onGone = null; }
            }
        });
        box.addView(sv, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        box.setFocusable(true);
        box.setFocusableInTouchMode(true);
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.setTitle("Banana quick menu");
        try {
            wm.addView(box, lp);
        } catch (Exception e) {
            Log.w(Core.TAG, "quick sheet: the overlay wasn't allowed", e);
            failed.run();
            return;
        }
        view = box;
        box.requestFocus();
    }

    static void hide(Runnable done) {
        if (view == null) { done.run(); return; }
        onGone = done;
        try { wm.removeView(view); } catch (Exception e) { Log.w(Core.TAG, "quick sheet", e); }
        view = null;
        // (no surfaceDestroyed came: done all the same)
        Core.ui.postDelayed(() -> { if (onGone != null) { onGone.run(); onGone = null; } }, 300);
    }

    private QuickSheet() { }
}
