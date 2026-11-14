package org.bananaos.launcher;

import android.util.Log;

/**
 * The Thor's button under the bottom screen (KEY_F24 under LineageOS; the pad's BTN_MODE too), as on BananaOS's image:
 *
 *   tap   over a game: the menu comes forward (the game keeps running behind it, as an Android app in the background);
 *         in the menu with a game behind it: back to the game; while the quick sheet is up: the sheet goes down
 *   hold  three seconds over a game: the quick menu slides up on the bottom screen as an overlay, the game going on
 *
 * Its presses come from two places: the window in focus (Keys: the launcher's own, or the quick sheet's) and banana-hwd
 * (Hwd: the daemon reads the button's input device for the launcher, since a game in front has the focus). The same
 * press can come from both: a down while already down, or an up while up, is the other one's and is dropped.
 */
final class HomeButton {
    static final int FROM_KEYS = 0, FROM_HWD = 1;
    static final long HOLD_MS = 3000;
    // the core's code for the button (root/etc/banana/hw.conf's HW_HOME_KEY): sent to it to close the quick sheet
    static final int CORE_CODE = Keys.KEY_F24;

    private static boolean down, held;
    private static final Runnable hold = HomeButton::hold;

    static void event(boolean isDown, int from) {
        Core.ui.post(() -> {
            if (isDown == down) return;
            down = isDown;
            if (isDown) { held = false; Core.ui.postDelayed(hold, HOLD_MS); return; }
            Core.ui.removeCallbacks(hold);
            if (!held) tap();
        });
    }

    private static void tap() {
        if (QuickSheet.showing()) { Core.nativeKey(true, CORE_CODE); Core.nativeKey(false, CORE_CODE); return; }
        if (HomeActivity.inFront()) { if (Games.running()) Games.backToGame(Core.app); return; }
        Games.toLauncher(Core.app);
    }

    /** the daemon's own hold (it times the button too): the same as this one's timer, whichever comes first */
    static void held() { Core.ui.post(() -> { if (down) { Core.ui.removeCallbacks(hold); hold(); } }); }

    private static void hold() {
        if (held) return;
        held = true;
        // (in the menu itself a hold is nothing, as on BananaOS: the quick menu is a game's)
        if (QuickSheet.showing() || HomeActivity.inFront()) return;
        Log.i(Core.TAG, "home held: the quick menu");
        Core.nativeHome(2);
    }

    private HomeButton() { }
}
