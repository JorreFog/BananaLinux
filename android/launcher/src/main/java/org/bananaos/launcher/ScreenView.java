package org.bananaos.launcher;

import android.content.Context;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * One of the menu's screens: a surface the core draws on (slot), and its touches as fractions of the surface (the core
 * fits its canvas into it: ui/src/panelfit.h). Keys and sticks go to the core from whichever screen has the focus.
 */
class ScreenView extends SurfaceView implements SurfaceHolder.Callback {
    private final int slot, screen;

    ScreenView(Context c, int slot, int screen) {
        super(c);
        this.slot = slot;
        this.screen = screen;
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override public void surfaceCreated(SurfaceHolder h) { }
    @Override public void surfaceChanged(SurfaceHolder h, int format, int w, int hh) { Core.nativeSurface(slot, h.getSurface()); }
    // (returns once the core isn't drawing on it any more)
    @Override public void surfaceDestroyed(SurfaceHolder h) { Core.nativeSurface(slot, null); }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
        int a;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: a = 0; break;
            case MotionEvent.ACTION_MOVE: a = 1; break;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: a = 2; break;
            default: return true;	// (one finger: the menu's touch is a single point)
        }
        Core.nativeTouch(screen, a, e.getX(0) / w, e.getY(0) / h, w > h);
        return true;
    }

    @Override public boolean dispatchKeyEvent(KeyEvent e) { return Keys.key(e) || super.dispatchKeyEvent(e); }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) { return Keys.motion(e) || super.dispatchGenericMotionEvent(e); }
}
