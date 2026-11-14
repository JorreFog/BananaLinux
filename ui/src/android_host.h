#pragma once
// banana-ui on Android: the bridge between the menu's core (main.cpp, unchanged but for three #ifdef __ANDROID__
// places) and the launcher app around it (android/launcher). The app's process runs the core's main() on a thread of
// its own, once; the activities and the quick sheet hand it their surfaces (android_display.cpp), their keys and
// touches (android_input.cpp), and the core asks the app to start games and show the sheet (android_jni.cpp).
//
//   surfaces  0 the top screen (HomeActivity, display 0), 1 the bottom (BottomActivity, display 1), 2 the quick sheet
//             (an overlay window on display 1 over a game)
#include <string>
#include <vector>
#include "input.h"

struct ANativeWindow;

namespace banana
{
	struct SystemDef;

	namespace android
	{
		// ---- surfaces (android_display.cpp) ----
		// a surface came (with its size), changed or went (nullptr); called on the app's UI thread. Going away waits
		// until the core isn't drawing on it
		void setWindow(int slot, ANativeWindow* w);
		// display 1's size as the system reports it, before its activity is up (0 x 0: no second display)
		void setBottomSize(int w, int h);

		// ---- input (android_input.cpp) ----
		// a key, a stick or a touch from the app (UI thread): queued for the core, which reads it in its loop
		void pushKey(bool down, int evdevCode, double t);
		void pushAxes(int device, float x, float y, float hatX, float hatY, double t);
		// a touch on screen s (0 top, 1 bottom) at (nx, ny), fractions of its surface's width and height; wide: the
		// surface is wider than tall. action 0 down, 1 move, 2 up
		void pushTouch(int s, int action, float nx, float ny, bool wide, double t);
		// wakes the core's poll() (the eventfd its loop polls as an input fd)
		void wake();

		// ---- the Home button (android_jni.cpp: the app decides tap or hold, the core shows the sheet) ----
		// 2: the quick menu over the game (the button held three seconds)
		void askHome(int what);
		int takeHomeAsk();

		// ---- games (android_jni.cpp, through the launch table: android_launch.h) ----
		// the game started through its emulator's intent; false (with the reason logged and shown) when the table has
		// no entry for its system or the app couldn't start it. core: the system's core setting (settings.conf core.<id>).
		// page: set (and true) when the table names one of the launcher's own pages instead (package = banana:<page>)
		bool runGame(const SystemDef& sd, const std::string& rom, const std::string& core, const std::string& perf, std::string& page);
		// the launcher came back over a game started from it: that game and how long it ran (once per start)
		bool takeGameBack(std::string& rom, long& secs);

		// ---- the quick sheet's overlay (android_jni.cpp) ----
		// the overlay window on display 1 shown (true: its surface is there, slot 2) or taken away
		bool showSheet(bool on);

		// ---- the platform (android_platform.cpp) ----
		// a call into the app's Java side: org.bananaos.launcher.Platform.call(args) -> its answer ("" if none). Any
		// thread (attached to the VM as needed)
		std::string javaCall(const std::vector<std::string>& args, int* status = nullptr);
		// a request to banana-hwd (android/system/hwd/PROTOCOL.md): its answer lines, false when it isn't there
		bool hwd(const std::string& request, std::string* answer = nullptr);

		// the tools' layer (android_tools.cpp): its thread for the commands of forked children, started before the core;
		// and a command line run as popen() would run it there
		void toolsInit();
		int runShell(const std::string& cmd, std::string& out);

		// where the app keeps the tree the core's fixed paths are mapped into (android_paths.cpp): <files>/root
		const std::string& root();
		void setRoot(const std::string& r);
	}
}
