package org.bananaos.launcher;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;

/**
 * banana-hwd's events for the launcher (android/system/hwd/PROTOCOL.md): the Home button's presses while a game has
 * the focus. The daemon reads the button's input device itself (without grabbing it), times the hold (3 s) and tells
 * its subscribers:
 *
 *   > SUBSCRIBE
 *   < OK
 *   < EVENT home down
 *   < EVENT home tap          (let go before the hold)
 *   < EVENT home hold         (still held at 3 s)
 *   < EVENT home release      (let go after a hold)
 *
 * Connected again every 10 s while the daemon isn't there (an image without it: the button then works only while the
 * launcher or its quick sheet has the focus, and a game is reached through Android's own HOME key).
 */
final class Hwd {
    static final String SOCKET = "/dev/socket/banana-hwd";

    static void listen() {
        Thread t = new Thread(() -> {
            boolean said = false;
            for (;;) {
                try (LocalSocket s = new LocalSocket()) {
                    s.connect(new LocalSocketAddress(SOCKET, LocalSocketAddress.Namespace.FILESYSTEM));
                    OutputStream out = s.getOutputStream();
                    out.write("SUBSCRIBE\n".getBytes());
                    out.flush();
                    BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream()));
                    Log.i(Core.TAG, "hwd: subscribed to the Home button");
                    for (String l; (l = in.readLine()) != null;) {
                        switch (l.trim()) {
                            case "EVENT home down": HomeButton.event(true, HomeButton.FROM_HWD); break;
                            case "EVENT home hold": HomeButton.held(); break;
                            case "EVENT home tap": case "EVENT home release": HomeButton.event(false, HomeButton.FROM_HWD); break;
                        }
                    }
                } catch (Exception e) {
                    if (!said) { Log.i(Core.TAG, "hwd: " + e.getMessage() + " (trying again every 10 s)"); said = true; }
                }
                try { Thread.sleep(10000); } catch (InterruptedException e) { return; }
            }
        }, "banana-hwd");
        t.setDaemon(true);
        t.start();
    }

    /** one request on a connection of its own: the daemon's answer after "OK", or null (ERR, or no daemon) */
    static String request(String line) {
        try (LocalSocket s = new LocalSocket()) {
            s.connect(new LocalSocketAddress(SOCKET, LocalSocketAddress.Namespace.FILESYSTEM));
            s.setSoTimeout(1500);
            OutputStream out = s.getOutputStream();
            out.write((line + "\n").getBytes());
            out.flush();
            String r = new BufferedReader(new InputStreamReader(s.getInputStream())).readLine();
            return r != null && r.startsWith("OK") ? r.substring(Math.min(3, r.length())) : null;
        } catch (Exception e) {
            return null;
        }
    }

    private Hwd() { }
}
