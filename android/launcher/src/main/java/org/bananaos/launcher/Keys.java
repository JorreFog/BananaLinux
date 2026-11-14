package org.bananaos.launcher;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/**
 * Android's keys and sticks as the kernel's codes, the ones the core reads on BananaOS's own image (ui/src/input.cpp's
 * table). A pad's button with a scan code in the kernel's gamepad range keeps it, so the buttons mean what they mean
 * there whatever Android's key layout calls them; the rest go by Android's key code.
 */
final class Keys {
    // linux/input-event-codes.h
    static final int KEY_ESC = 1, KEY_BACKSPACE = 14, KEY_TAB = 15, KEY_ENTER = 28, KEY_S = 31, KEY_X = 45, KEY_Y = 21, KEY_F1 = 59,
        KEY_PAGEUP = 104, KEY_PAGEDOWN = 109, KEY_F24 = 194,
        BTN_SOUTH = 304, BTN_EAST = 305, BTN_C = 306, BTN_NORTH = 307, BTN_WEST = 308, BTN_Z = 309, BTN_TL = 310, BTN_TR = 311,
        BTN_TL2 = 312, BTN_TR2 = 313, BTN_SELECT = 314, BTN_START = 315, BTN_MODE = 316, BTN_THUMBL = 317, BTN_THUMBR = 318,
        BTN_DPAD_UP = 544, BTN_DPAD_DOWN = 545, BTN_DPAD_LEFT = 546, BTN_DPAD_RIGHT = 547;

    /** the Thor's button under the bottom screen: LineageOS's GPIO key sends KEY_F24; the pad's mode button too */
    static boolean isHome(int code) { return code == KEY_F24 || code == BTN_MODE; }

    /** the kernel's code for this key, or -1 for one the menu leaves to the system (volume, power) */
    static int evdev(KeyEvent e) {
        int kc = e.getKeyCode();
        switch (kc) {
            case KeyEvent.KEYCODE_VOLUME_UP: case KeyEvent.KEYCODE_VOLUME_DOWN: case KeyEvent.KEYCODE_VOLUME_MUTE:
            case KeyEvent.KEYCODE_POWER: case KeyEvent.KEYCODE_HOME: case KeyEvent.KEYCODE_APP_SWITCH:
                return -1;
        }
        int sc = e.getScanCode();
        if (sc >= 0x100 && sc < 0x300) return sc;	// (BTN_*: the pad's own)
        switch (kc) {
            case KeyEvent.KEYCODE_BUTTON_A: return BTN_SOUTH;
            case KeyEvent.KEYCODE_BUTTON_B: return BTN_EAST;
            case KeyEvent.KEYCODE_BUTTON_C: return BTN_C;
            case KeyEvent.KEYCODE_BUTTON_X: return BTN_NORTH;
            case KeyEvent.KEYCODE_BUTTON_Y: return BTN_WEST;
            case KeyEvent.KEYCODE_BUTTON_Z: return BTN_Z;
            case KeyEvent.KEYCODE_BUTTON_L1: return BTN_TL;
            case KeyEvent.KEYCODE_BUTTON_R1: return BTN_TR;
            case KeyEvent.KEYCODE_BUTTON_L2: return BTN_TL2;
            case KeyEvent.KEYCODE_BUTTON_R2: return BTN_TR2;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return BTN_SELECT;
            case KeyEvent.KEYCODE_BUTTON_START: return BTN_START;
            case KeyEvent.KEYCODE_BUTTON_MODE: return BTN_MODE;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return BTN_THUMBL;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return BTN_THUMBR;
            case KeyEvent.KEYCODE_DPAD_UP: return BTN_DPAD_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return BTN_DPAD_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return BTN_DPAD_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return BTN_DPAD_RIGHT;
            case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return KEY_ENTER;
            case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE: return KEY_ESC;
            case KeyEvent.KEYCODE_DEL: return KEY_BACKSPACE;
            case KeyEvent.KEYCODE_TAB: return KEY_TAB;
            case KeyEvent.KEYCODE_PAGE_UP: return KEY_PAGEUP;
            case KeyEvent.KEYCODE_PAGE_DOWN: return KEY_PAGEDOWN;
            case KeyEvent.KEYCODE_S: return KEY_S;
            case KeyEvent.KEYCODE_X: return KEY_X;
            case KeyEvent.KEYCODE_Y: return KEY_Y;
            case KeyEvent.KEYCODE_F1: return KEY_F1;
        }
        return sc > 0 ? sc : -1;	// (a key Android has no name for: its scan code, KEY_F24 among them)
    }

    /** a key for the core (true: taken). The Home button goes to HomeButton, which decides tap or hold */
    static boolean key(KeyEvent e) {
        int code = evdev(e);
        if (code < 0) return false;
        if (e.getRepeatCount() > 0) return true;	// (the core repeats a held direction itself)
        boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        if (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_UP) return true;
        if (isHome(code)) { HomeButton.event(down, HomeButton.FROM_KEYS); return true; }
        Core.nativeKey(down, code);
        return true;
    }

    /** a pad's left stick and hat (true: taken) */
    static boolean motion(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK || e.getAction() != MotionEvent.ACTION_MOVE) return false;
        Core.nativeAxes(e.getDeviceId(), e.getAxisValue(MotionEvent.AXIS_X), e.getAxisValue(MotionEvent.AXIS_Y),
            e.getAxisValue(MotionEvent.AXIS_HAT_X), e.getAxisValue(MotionEvent.AXIS_HAT_Y));
        return true;
    }

    private Keys() { }
}
