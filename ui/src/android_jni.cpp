// banana-ui on Android: the JNI side of the bridge (android_host.h). The launcher app (android/launcher, package
// org.bananaos.launcher) loads libbanana.so, which is the menu's whole core, and calls Core.nativeStart() once per
// process: the core's main() (main.cpp, compiled as banana_main) runs on a thread of its own from then on, for as long
// as the process lives, and drives both screens. The core calls back into Core's static methods to start a game
// (launch), show the quick sheet (showSheet), reach the platform (Platform.call) and say it ended (onExit).
#ifdef __ANDROID__
#include "android_host.h"
#include "android_launch.h"
#include "library.h"
#include "settings.h"
#include "vkbd.h"
#include <android/log.h>
#include <android/native_window_jni.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <jni.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

int banana_main(int argc, char** argv);

// (vkbd.h's in-class constant is bound to a reference in vkbd.cpp: GCC's -O2 build inlines it, clang's debug build
// needs its one definition)
const int banana::VirtualKeyboard::KEY_HOLD_MS;	// main.cpp's main() (CMakeLists.txt: -Dmain=banana_main for that file)

namespace banana
{
	namespace android
	{
		namespace
		{
			JavaVM* gVm = nullptr;
			jclass gCore = nullptr, gPlatform = nullptr;
			jmethodID mLaunch = nullptr, mShowSheet = nullptr, mOnExit = nullptr, mCall = nullptr;
			pthread_key_t gDetach;
			std::atomic<int> homeAsk{ 0 };
			std::mutex backM;
			std::string backRom;
			long backSecs = -1;

			// the calling thread's JNIEnv, attached to the VM the first time (and detached when the thread ends)
			JNIEnv* env()
			{
				JNIEnv* e = nullptr;
				if (!gVm) return nullptr;
				if (gVm->GetEnv((void**)&e, JNI_VERSION_1_6) == JNI_OK) return e;
				JavaVMAttachArgs a = { JNI_VERSION_1_6, "banana-core", nullptr };
				if (gVm->AttachCurrentThread(&e, &a) != JNI_OK) return nullptr;
				pthread_setspecific(gDetach, e);
				return e;
			}

			jobjectArray strings(JNIEnv* e, const std::vector<std::string>& v)
			{
				jobjectArray a = e->NewObjectArray((jsize)v.size(), e->FindClass("java/lang/String"), nullptr);
				for (size_t i = 0; i < v.size(); i++)
				{
					jstring s = e->NewStringUTF(v[i].c_str());
					e->SetObjectArrayElement(a, (jsize)i, s);
					e->DeleteLocalRef(s);
				}
				return a;
			}

			std::string str(JNIEnv* e, jstring s)
			{
				if (!s) return "";
				const char* c = e->GetStringUTFChars(s, nullptr);
				std::string r = c ? c : "";
				if (c) e->ReleaseStringUTFChars(s, c);
				return r;
			}

			void clearException(JNIEnv* e)
			{
				if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); }
			}

			// stdout and stderr (the core's log: "banana-ui: ...") to logcat, and to <root>/run/banana/banana-ui.log
			void logPipe()
			{
				int fd[2];
				if (pipe(fd) != 0) return;
				dup2(fd[1], 1); dup2(fd[1], 2);
				close(fd[1]);
				setvbuf(stdout, nullptr, _IOLBF, 0);
				setvbuf(stderr, nullptr, _IONBF, 0);
				std::thread([r = fd[0]] {
					FILE* in = fdopen(r, "r");
					FILE* file = fopen("/run/banana/banana-ui.log", "w");	// (mapped into the app's tree)
					char line[2048];
					while (in && fgets(line, sizeof line, in))
					{
						__android_log_write(ANDROID_LOG_INFO, "banana-ui", line);
						if (file) { fputs(line, file); fflush(file); }
					}
				}).detach();
			}
		}

		std::string javaCall(const std::vector<std::string>& args, int* status)
		{
			if (status) *status = 1;
			JNIEnv* e = env();
			if (!e || !gPlatform || !mCall) return "";
			e->PushLocalFrame(16);
			jobjectArray a = strings(e, args);
			jstring r = (jstring)e->CallStaticObjectMethod(gPlatform, mCall, a);
			clearException(e);
			// "<status>\n<output>"
			std::string s = str(e, r);
			e->PopLocalFrame(nullptr);
			size_t nl = s.find('\n');
			if (status) *status = atoi(s.substr(0, nl).c_str());
			return nl == std::string::npos ? "" : s.substr(nl + 1);
		}

		bool showSheet(bool on)
		{
			JNIEnv* e = env();
			if (!e || !mShowSheet) return false;
			jboolean ok = e->CallStaticBooleanMethod(gCore, mShowSheet, (jboolean)on);
			clearException(e);
			return ok;
		}

		void askHome(int what) { homeAsk = what; wake(); }
		int takeHomeAsk() { return homeAsk.exchange(0); }

		// the layers of the launch table (android_launch.h); the card's settings.conf for launch.<id>
		static int loadTable(launch::Table& t)
		{
			const char* data = getenv("BANANA_DATA");
			int files = t.loadAll(root() + "/etc/banana/launch.conf", std::string(data ? data : "") + "/launch.conf");
			for (auto& w : t.warnings) fprintf(stderr, "banana-ui: launch table: %s\n", w.c_str());
			return files;
		}

		bool runGame(const SystemDef& sd, const std::string& rom, const std::string& core, const std::string& perf, std::string& page)
		{
			page.clear();
			launch::Table t;
			int files = loadTable(t);
			// (another emulator for the system: settings.conf's launch.<id> = <variant>, the table's [<id>.<variant>])
			const char* data = getenv("BANANA_DATA");
			const std::string variant = getConfValue(std::string(data ? data : "") + "/settings.conf", "launch." + sd.id, "");
			const launch::Entry* en = t.find(sd.id, variant);
			// (one of the launcher's own pages: the core opens it; the web browser is Android's own)
			if (en && !launch::pageOf(*en).empty() && launch::pageOf(*en) != "browser") { page = launch::pageOf(*en); return true; }
			std::vector<std::string> spec;
			if (!en)
			{
				fprintf(stderr, "banana-ui: launch: no emulator for %s in the launch table (%d files read)\n", sd.id.c_str(), files);
				spec.push_back("error=No emulator is set up for " + sd.name + " (launch.conf)");
			}
			else
			{
				spec = launch::intentSpec(*en, rom, launch::coreOf(sd.run, core, en->core));
				spec.push_back("perf=" + perf);
				fprintf(stderr, "banana-ui: launch %s: %s (%s)\n", sd.id.c_str(), en->package.c_str(), en->file.c_str());
			}
			JNIEnv* e = env();
			if (!e || !mLaunch) return false;
			e->PushLocalFrame(16);
			jstring r = (jstring)e->CallStaticObjectMethod(gCore, mLaunch, strings(e, spec));
			clearException(e);
			std::string err = str(e, r);
			e->PopLocalFrame(nullptr);
			if (!err.empty()) fprintf(stderr, "banana-ui: launch %s: %s\n", sd.id.c_str(), err.c_str());
			// (the daemon: the game's clock profile, as banana-perf on BananaOS, and its system's colour on the stick lights)
			if (err.empty() && en) hwd("GAME START " + sd.id + " " + (perf.empty() ? std::string("balanced") : perf));
			return en && err.empty();
		}

		bool takeGameBack(std::string& rom, long& secs)
		{
			std::lock_guard<std::mutex> l(backM);
			if (backSecs < 0) return false;
			rom = backRom; secs = backSecs;
			backSecs = -1;
			return true;
		}
	}
}

using namespace banana;

extern "C"
{
	JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
	{
		android::gVm = vm;
		pthread_key_create(&android::gDetach, [](void*) { if (android::gVm) android::gVm->DetachCurrentThread(); });
		JNIEnv* e = nullptr;
		if (vm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) return -1;
		jclass c = e->FindClass("org/bananaos/launcher/Core");
		jclass p = e->FindClass("org/bananaos/launcher/Platform");
		if (!c || !p) return -1;
		android::gCore = (jclass)e->NewGlobalRef(c);
		android::gPlatform = (jclass)e->NewGlobalRef(p);
		android::mLaunch = e->GetStaticMethodID(c, "launch", "([Ljava/lang/String;)Ljava/lang/String;");
		android::mShowSheet = e->GetStaticMethodID(c, "showSheet", "(Z)Z");
		android::mOnExit = e->GetStaticMethodID(c, "onExit", "(I)V");
		android::mCall = e->GetStaticMethodID(p, "call", "([Ljava/lang/String;)Ljava/lang/String;");
		return JNI_VERSION_1_6;
	}

	// root: the app's tree (<files>/root); env: "NAME=value" for the core; args: its command line after the name
	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeStart(JNIEnv* e, jclass, jstring root, jobjectArray env, jobjectArray args)
	{
		static std::once_flag once;
		std::string r = android::str(e, root);
		std::vector<std::string> ev, av{ "banana-ui" };
		for (jsize i = 0; env && i < e->GetArrayLength(env); i++) ev.push_back(android::str(e, (jstring)e->GetObjectArrayElement(env, i)));
		for (jsize i = 0; args && i < e->GetArrayLength(args); i++) av.push_back(android::str(e, (jstring)e->GetObjectArrayElement(args, i)));
		std::call_once(once, [&] {
			android::setRoot(r);
			for (auto& kv : ev)
			{
				size_t eq = kv.find('=');
				if (eq != std::string::npos) setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
			}
			android::logPipe();
			android::toolsInit();
			// the systems the launch table has no Android route for (package = none) left off the shelves: the core
			// reads a copy of systems.conf without them
			for (size_t i = 1; i + 1 < av.size(); i++)
				if (av[i] == "--systems")
				{
					launch::Table t;
					android::loadTable(t);
					std::string to = r + "/run/banana/systems.conf";
					if (launch::filterSystems(t, av[i + 1], to)) av[i + 1] = to;
				}
			std::thread([av] {
				std::vector<char*> argv;
				for (auto& s : av) argv.push_back(const_cast<char*>(s.c_str()));
				argv.push_back(nullptr);
				int code = banana_main((int)av.size(), argv.data());
				fprintf(stderr, "banana-ui: the core ended (%d)\n", code);
				if (JNIEnv* je = android::env()) { je->CallStaticVoidMethod(android::gCore, android::mOnExit, (jint)code); android::clearException(je); }
			}).detach();
		});
	}

	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeSurface(JNIEnv* e, jclass, jint slot, jobject surface)
	{
		ANativeWindow* w = surface ? ANativeWindow_fromSurface(e, surface) : nullptr;
		android::setWindow(slot, w);
		if (w) ANativeWindow_release(w);	// (setWindow holds its own reference)
	}

	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeBottomSize(JNIEnv*, jclass, jint w, jint h) { android::setBottomSize(w, h); }

	static double monoMs()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
	}

	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeKey(JNIEnv*, jclass, jboolean down, jint code) { android::pushKey(down, code, monoMs()); }
	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeAxes(JNIEnv*, jclass, jint dev, jfloat x, jfloat y, jfloat hx, jfloat hy)
	{
		android::pushAxes(dev, x, y, hx, hy, monoMs());
	}
	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeTouch(JNIEnv*, jclass, jint s, jint action, jfloat nx, jfloat ny, jboolean wide)
	{
		android::pushTouch(s, action, nx, ny, wide, monoMs());
	}
	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeHome(JNIEnv*, jclass, jint what) { android::askHome(what); }
	JNIEXPORT void JNICALL Java_org_bananaos_launcher_Core_nativeGameBack(JNIEnv* e, jclass, jstring rom, jlong secs)
	{
		std::lock_guard<std::mutex> l(android::backM);
		android::backRom = android::str(e, rom);
		android::backSecs = (long)secs;
		android::wake();
	}
	// a tool's command line run on the platform, as the core would (for the app's own checks: adb's am broadcast)
	JNIEXPORT jstring JNICALL Java_org_bananaos_launcher_Core_nativeShell(JNIEnv* e, jclass, jstring cmd)
	{
		std::string out;
		int st = android::runShell(android::str(e, cmd), out);
		return e->NewStringUTF((std::to_string(st) + "\n" + out).c_str());
	}
}
#endif
