package org.bananaos.launcher;

import android.app.Application;

/** The launcher's process: the core starts with it, before any activity (both screens' activities attach to it). */
public class App extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        Core.start(this);
    }
}
