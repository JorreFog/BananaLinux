// banana-ui: Network > Home server's pages of its own, for pairing. Which kind of server first (RomM or Romhost: the
// sync has one at a time), then:
//   Romhost  as it always was: looked for on the LAN (its UDP answer), its address typed when none answers, then the
//            6-digit code Homeport shows (settings.cpp's startPairing and pollSync)
//   RomM     a form as Grout's (Protocol, Address, Port, Check certificate, Connect): its address is a URL, on the LAN
//            or the internet (a Cloudflare tunnel, a reverse proxy), never guessed. Connect asks banana-sync probe
//            --romm, which asks that URL's heartbeat alone, follows no redirect and says why it isn't RomM when it
//            isn't (no such name, refused, no answer in time, the certificate, the clock, a redirect, Cloudflare
//            Access's login, 401 or 403, something not RomM), each said here in words of its own. Then the pairing
//            screen: RomM's code large on the top panel with the page that approves it written out and the time left,
//            and that page as a QR code on the bottom one, for the phone (where the owner is signed in to RomM, and
//            through Cloudflare Access's login when there is one: the handheld only ever talks to the API). X types a
//            client token's pairing code instead.
// On the device as on the first try (2026-11-05): a bare name made the old probe try http on 8092, 8080 and 80 before
// https, and Cloudflare answers 8080 with a redirect to https that libcurl followed, so the handheld paired at
// http://<name>:8080 and RomM's device pairing then answered 405 (the redirect turned its POST into a GET).
#include "settings.h"
#include "settings_draw.h"
#include "gamesync.h"
#include "json.h"
#include "qr/qrcodegen.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <unistd.h>

using rnds::Color;

namespace banana
{
	namespace
	{
		enum { F_PROTO, F_ADDR, F_PORT, F_TLS, F_CONNECT };
		std::string lowerOf(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
		// a URL's host (and port), for the messages: "romm.example.com"
		std::string hostOf(const std::string& url)
		{
			size_t a = url.find("://");
			a = a == std::string::npos ? 0 : a + 3;
			size_t b = url.find('/', a);
			return url.substr(a, b == std::string::npos ? std::string::npos : b - a);
		}
		std::string firstLine(const std::string& path)
		{
			std::ifstream f(path);
			std::string l;
			std::getline(f, l);
			return l;
		}
	}

	// ---- the state and the address -------------------------------------------------------------------------------------
	std::string Settings::hsBase() const
	{
		return (hsHttps ? "https://" : "http://") + hsHost + (hsPort.empty() ? "" : ":" + hsPort) + hsPath;
	}

	void Settings::hsSetAddress(const std::string& typed)
	{
		// a whole URL (pasted, or typed with https://) fills the rows it names: its scheme, its port and a path (RomM served under a subpath)
		std::string a = bsync::trim(typed);
		size_t sc = a.find("://");
		bool whole = sc != std::string::npos;
		if (whole) { hsHttps = lowerOf(a.substr(0, sc)) != "http"; a = a.substr(sc + 3); }
		size_t sl = a.find('/');
		std::string hp = a.substr(0, sl);
		hsPath = sl == std::string::npos ? "" : a.substr(sl);
		while (!hsPath.empty() && hsPath.back() == '/') hsPath.pop_back();
		size_t c = hp.rfind(':'), br = hp.find(']');
		if (c != std::string::npos && (br == std::string::npos || c > br))
		{
			std::string p = hp.substr(c + 1);
			hp = hp.substr(0, c);
			int n = p.empty() || p.find_first_not_of("0123456789") != std::string::npos ? 0 : atoi(p.c_str());
			hsPort = n < 1 || n > 65535 || n == (hsHttps ? 443 : 80) ? "" : std::to_string(n);
		}
		else if (whole) hsPort.clear();
		hsHost = hp;
		hsMsg.clear(); hsCause.clear(); hsMoved.clear();
	}

	std::vector<int> Settings::hsFormRows() const
	{
		std::vector<int> r = { F_PROTO, F_ADDR, F_PORT };
		if (hsHttps) r.push_back(F_TLS);
		r.push_back(F_CONNECT);
		return r;
	}

	void Settings::openHome(HsView v)
	{
		page = HOMESRV;
		hsView = v;
		hsSel = 0;
		if (v == HS_FORM)
		{
			// (RomM paired already: its address to start from, to pair again or move it)
			if (hsHost.empty() && hsBackend() == "romm") hsSetAddress(hsPairedUrl);
			hsSel = hsHost.empty() ? 1 : (int)hsFormRows().size() - 1;
		}
		if (v != HS_PAIR) { hsMsg.clear(); hsCause.clear(); }
		message.clear();
	}

	// ---- RomM: the probe, the pairing ----------------------------------------------------------------------------------
	void Settings::hsConnect()
	{
		if (hsStep == 4 || hsStep == 3) return;
		if (hsHost.empty()) { hsMsg = "Type RomM's address first."; hsSel = 1; return; }
		if (!wifiOn()) { hsMsg = "Wi-Fi is off: switch it on in Network first."; return; }
		hsStep = 4;
		hsCause.clear(); hsMoved.clear();
		hsMsg = "Checking " + hsBase() + "...";
		hsJob.start(syncCmd("probe --romm " + gamesync::shellQuote(hsBase())) + " 2>/dev/null", sync);
	}

	// the RomM a probe found, from RomM's own form or an address typed where Romhost's was asked for: its name, and the
	// note a RomM before 5.4 has on the pairing screen
	void Settings::hsRommFound(const std::string& url, const std::string& ver, bool old)
	{
		hsKind = "romm"; hsUrl = url; hsName = "RomM " + ver;
		hsNote = old ? "RomM " + ver + ": until it's 5.4 or newer, saves only come down from it, none go up." : "";
		fprintf(stderr, "home server: RomM %s at %s\n", ver.c_str(), url.c_str());	// (what a test waits for)
	}

	void Settings::hsProbed(const std::string& out)
	{
		hsStep = 0;
		std::string ln = out.substr(0, out.find('\n'));
		std::istringstream in(ln);
		std::string kind;
		in >> kind;
		if (kind == "romm")
		{
			// "romm <url> RomM <version> [old]"
			std::string url, r, ver, old;
			in >> url >> r >> ver >> old;
			hsRommFound(url, ver, old == "old");
			hsStartPair("");
			return;
		}
		std::string detail;
		in >> kind;	// (after "fail")
		std::getline(in, detail);
		detail = bsync::trim(detail);
		hsCause = kind;
		hsMoved.clear();
		if (kind == "redirect")
		{
			// a redirect to the same API elsewhere (http to https, another port): that address offered (Y)
			const std::string hb = "/api/heartbeat";
			if (detail.size() > hb.size() && detail.compare(detail.size() - hb.size(), hb.size(), hb) == 0 && detail.find("://") != std::string::npos)
				hsMoved = detail.substr(0, detail.size() - hb.size());
		}
		hsMsg = ln.empty() ? std::string("banana-sync didn't answer.") : hsWhy(kind, detail);
		fprintf(stderr, "home server: no RomM: %s %s\n", kind.c_str(), detail.c_str());
	}

	// the banana-sync whose pid is in sync.pid, while it runs (a sync, or a pairing: both hold sync.lock); 0 when none
	static int runningSync(const std::string& runDir)
	{
		int pid = atoi(firstLine(runDir + "/sync.pid").c_str());
		std::string comm = pid > 0 ? bsync::trim(firstLine("/proc/" + std::to_string(pid) + "/comm")) : "";
		return pid > 0 && comm == "banana-sync" && kill(pid, 0) == 0 ? pid : 0;
	}

	void Settings::hsStartPair(const std::string& code)
	{
		hsKind = "romm";
		hsView = HS_PAIR;
		page = HOMESRV;
		hsDone = false;
		hsCode.clear(); hsVerify.clear(); hsCause.clear();
		hsExpires = 0;
		// (a sync running holds the lock a pairing needs: said here, its status left as it is, rather than a pairing
		// that can't start and a Cancel that would stop that sync)
		if (runningSync(runDir))
		{
			hsStep = 0;
			hsCause = "busy";
			hsMsg = hsWhy("busy", "");
			fprintf(stderr, "home server: pairing not started: a sync is running\n");
			return;
		}
		hsStep = 3;
		hsMsg = code.empty() ? "Asking " + hsName + " for a code..." : "Pairing with the code...";
		std::ofstream(runDir + "/sync.status") << "pairing\n";
		syncLine.clear();	// (the status read again even when its line is the one before)
		auto q = gamesync::shellQuote;
		hsRun("pair --romm " + q(hsUrl) + (code.empty() ? "" : " " + q(code)));
	}

	void Settings::hsPairLine(const std::string& ln)
	{
		if (ln.compare(0, 5, "pair ") == 0)
		{
			// "pair <code> <url> [<seconds>]"
			std::istringstream in(ln.substr(5));
			std::string code, url;
			int secs = 0;
			in >> code >> url >> secs;
			if (code != hsCode) hsExpires = now + (secs > 0 ? secs : 600) * 1000.0;
			hsCode = code; hsVerify = url;
			hsMsg.clear();
			fprintf(stderr, "home server: code %s, approve at %s\n", code.c_str(), url.c_str());
		}
		else if (ln.compare(0, 7, "paired ") == 0)
		{
			hsStep = 0;
			hsDone = true;
			JPtr j = readJsonFile(data + "/romsync.json");
			std::string dev = j && j->isObj() ? j->str("name") : "";
			std::string srv = ln.substr(7);
			// (each case a whole line, so a translation has the sentence entire; the sync before a game named whenever it
			// is on once something was turned on, since the save then comes both ways)
			const bool autoOn = hsTurnAutoOn(), launchOn = hsTurnLaunchOn();
			const bool both = (autoOn || launchOn) && getConfValue(data + "/settings.conf", "homesync_launch", "off") == "on";
			hsMsg = dev.empty() ? (both ? "Paired with " + srv + ". Saves now sync before you play and as you leave a game. Get games brings games over."
					: autoOn ? "Paired with " + srv + ". Saves now sync as you leave a game (Auto sync). Get games brings games over."
					: "Paired with " + srv + ". Get games brings games over.")
				: both ? "Paired as " + dev + " with " + srv + ". Saves now sync before you play and as you leave a game. Get games brings games over."
				: autoOn ? "Paired as " + dev + " with " + srv + ". Saves now sync as you leave a game (Auto sync). Get games brings games over."
				: "Paired as " + dev + " with " + srv + ". Get games brings games over.";
			fprintf(stderr, "home server: paired with %s\n", ln.substr(7).c_str());
			hsRebuild();
		}
		else if (ln.compare(0, 6, "error ") == 0)
		{
			hsStep = 0;
			std::string rest = ln.substr(6), cause = rest.substr(0, rest.find(' ')), detail = rest.find(' ') == std::string::npos ? "" : rest.substr(rest.find(' ') + 1);
			hsCause = cause;
			// (the device flow's own words first: "refused in RomM" isn't probe's refused)
			if (rest == "refused in RomM") hsMsg = "Refused in RomM. A asks for a new code.";
			else if (rest == "the code ran out") hsMsg = "The code ran out before it was approved. A asks for a new one.";
			else if (rest == "stopped") hsMsg = "Stopped.";
			else hsMsg = hsWhy(cause, detail);
			fprintf(stderr, "home server: pairing failed: %s\n", rest.c_str());
		}
	}

	void Settings::hsCancelPair()
	{
		// banana-sync pair writes its pid as a sync does (it holds the same lock); it says "error stopped" as it ends,
		// which nothing reads now: hsStep is 0. Waited for, a moment, so a pairing started next finds the lock free
		hsStep = 0;
		int pid = runningSync(runDir);
		if (pid > 0 && kill(pid, SIGTERM) == 0)
			for (int i = 0; i < 20 && kill(pid, 0) == 0; i++) usleep(50000);
	}

	std::string Settings::hsWhy(const std::string& cause, const std::string& detail) const
	{
		std::string url = hsStep == 3 || hsView == HS_PAIR ? hsUrl : hsBase(), host = hostOf(url);
		if (cause == "busy") return "A sync is running. Wait for it to end, or stop it on Sync now, then pair.";
		if (cause == "dns") return "Can't find " + host + ". Check the address, and that this Wi-Fi reaches the network RomM is on.";
		if (cause == "refused") return host + " refused the connection. Check the port, and HTTPS or HTTP.";
		if (cause == "timeout") return host + " didn't answer in time. Is it reachable from this Wi-Fi? Check the port, and HTTPS or HTTP.";
		if (cause == "tls") return "The server's certificate isn't trusted. For a certificate of your own making, Y turns off Check certificate (or put your CA's certificate on the card as homesync_ca).";
		if (cause == "clock") return "The certificate can't be checked yet: the clock isn't set from the internet. Wait a minute on Wi-Fi and try again.";
		if (cause == "ca") return "homesync_ca names a file that can't be read: check it in banana/settings.conf.";
		if (cause == "access") return "Cloudflare Access asks for a login there. Let RomM's API through: a Bypass policy for " + host + "/api in Access. Your phone still logs in, for the page that approves the handheld.";
		if (cause == "redirect")
			return !hsMoved.empty() ? "The server sends this address to " + hsMoved + ". Y uses that address."
				: "The server sends this address elsewhere, not to RomM: " + detail;
		if (cause == "denied") return host + " refused access (" + detail + "). Something in front of RomM may be blocking the handheld (a firewall, Cloudflare's bot protection, a login page): let /api through.";
		if (cause == "http") return host + " answered " + detail + " where RomM's API should be. Check the host, and the path if RomM is served under one (https://example.com/romm).";
		if (cause == "notromm") return "Something answers at " + url + ", but not RomM's API. Check the host, and the path if RomM is served under one (https://example.com/romm).";
		if (cause == "noanswer") return detail.empty() ? "No answer from " + host + "." : "No answer from " + host + " (" + detail + ").";
		std::string all = detail.empty() ? cause : cause + " " + detail;
		return "Pairing didn't work: " + all + ".";
	}

	// ---- the keys -------------------------------------------------------------------------------------------------------
	Settings::Result Settings::pressHome(Key k)
	{
		if (k == Key::START && hsView != HS_FORM) { page = LIST; mOpen = false; return CLOSED; }
		if (hsView == HS_CHOOSE)
		{
			if (k == Key::B) { page = LIST; return MOVED; }
			if (k == Key::UP || k == Key::DOWN) { hsSel = 1 - hsSel; return MOVED; }
			if (k != Key::A) return NONE;
			if (hsSel == 0) { openHome(HS_FORM); return MOVED; }
			// Romhost: looked for on the LAN, from the Server row as before
			page = LIST;
			startPairing();
			return MOVED;
		}
		if (hsView == HS_FORM)
		{
			std::vector<int> rows = hsFormRows();
			hsSel = std::min(hsSel, (int)rows.size() - 1);
			int row = rows[hsSel];
			bool busy = hsStep == 4;
			auto tlsOn = [&]() { return getConfValue(data + "/settings.conf", "homesync_tls", "on") != "off"; };
			auto setTls = [&](bool on)
			{
				setConfValue(data + "/settings.conf", "homesync_tls", on ? "on" : "off");
				for (auto& it : items) if (it.key == "homesync_tls") load(it);
			};
			switch (k)
			{
			case Key::B: if (busy) return NONE; hsView = HS_CHOOSE; hsSel = 0; hsMsg.clear(); return MOVED;
			case Key::UP: hsSel = (hsSel + (int)rows.size() - 1) % (int)rows.size(); return MOVED;
			case Key::DOWN: hsSel = (hsSel + 1) % (int)rows.size(); return MOVED;
			case Key::LEFT: case Key::RIGHT: case Key::A:
				if (busy) return NONE;
				if (row == F_PROTO)
				{
					hsHttps = !hsHttps;
					hsMsg.clear(); hsCause.clear();
					return MOVED;
				}
				if (row == F_TLS) { setTls(!tlsOn()); hsMsg.clear(); return MOVED; }
				if (k != Key::A) return NONE;
				if (row == F_ADDR) { startEntry(ENTRY_HS_URL, "RomM", "Its address", HOMESRV, hsHost + hsPath); return MOVED; }
				if (row == F_PORT) { startEntry(ENTRY_HS_PORT, "RomM", hsHttps ? "Port (443 if empty)" : "Port (80 if empty)", HOMESRV, hsPort); return MOVED; }
				hsConnect();
				return MOVED;
			case Key::START: hsConnect(); return MOVED;
			case Key::Y:
				if (busy) return NONE;
				if (hsCause == "tls" && hsHttps && tlsOn()) { setTls(false); hsMsg = "Check certificate is off: Connect tries again."; hsCause.clear(); return MOVED; }
				if (hsCause == "redirect" && !hsMoved.empty()) { hsSetAddress(hsMoved); hsSel = (int)hsFormRows().size() - 1; return MOVED; }
				return NONE;
			default: return NONE;
			}
		}
		// the pairing screen
		switch (k)
		{
		case Key::B:
			if (hsDone) { page = LIST; return MOVED; }
			if (hsStep == 3) hsCancelPair();
			hsView = HS_FORM;
			hsMsg.clear();
			return MOVED;
		case Key::A:
			if (hsDone)
			{
				page = LIST;
				for (size_t i = 0; i < items.size(); i++) if (items[i].key == "hs_sync") { sel = (int)i; break; }
				top = std::max(groupStart(groupOf(sel)), std::min(sel, std::max(0, groupEnd(groupOf(sel)) - ROWS)));
				return MOVED;
			}
			if (hsStep == 0) { hsStartPair(""); return MOVED; }
			return NONE;
		case Key::X:
			if (hsDone) return NONE;
			if (hsStep == 3) hsCancelPair();
			startEntry(ENTRY_HS_CODE, "RomM", "Pairing code", HOMESRV);
			return MOVED;
		default: return NONE;
		}
	}

	std::vector<Settings::Chip> Settings::homeChips()
	{
		std::vector<Chip> cs = { { Key::B, "B", "Back" } };
		if (hsView == HS_CHOOSE) { cs.push_back({ Key::A, "A", "Choose", true }); return cs; }
		if (hsView == HS_FORM)
		{
			if (hsStep == 4) return cs;
			if ((hsCause == "tls" && hsHttps) || (hsCause == "redirect" && !hsMoved.empty())) cs.push_back({ Key::Y, "Y", hsCause == "tls" ? "Don't check" : "Use it" });
			std::vector<int> rows = hsFormRows();
			int row = rows[std::min(hsSel, (int)rows.size() - 1)];
			cs.push_back({ Key::A, "A", row == F_CONNECT ? "Connect" : row == F_PROTO || row == F_TLS ? "Change" : "Type", true });
			return cs;
		}
		if (hsDone) { cs[0].label = "Done"; cs.push_back({ Key::A, "A", "Sync now", true }); return cs; }
		cs[0].label = hsStep == 3 ? "Cancel" : "Back";
		cs.push_back({ Key::X, "X", "Use a code" });
		if (hsStep == 0) cs.push_back({ Key::A, "A", "Try again", true });
		return cs;
	}

	Settings::Result Settings::touchHome(float cx, float cy)
	{
		(void)cx; (void)cy;
		if (downY >= 418 && downY <= 466) return pressHome(chipAt(homeChips(), downX));
		if (hsView == HS_PAIR) return NONE;
		int n = hsView == HS_CHOOSE ? 2 : (int)hsFormRows().size();
		for (int r = 0; r < n && r < ROWS; r++)
		{
			float ry = 82 + r * 64;
			if (downY < ry || downY > ry + 56) continue;
			if (r != hsSel) { hsSel = r; return MOVED; }
			return pressHome(Key::A);
		}
		return NONE;
	}

	// ---- drawing --------------------------------------------------------------------------------------------------------
	void Settings::renderHome()
	{
		SPal p = pal(light);
		const bool paired = !homeServer().empty();
		std::vector<int> rows = hsFormRows();
		if (hsView == HS_FORM) hsSel = std::min(hsSel, (int)rows.size() - 1);

		// ---- top: what is chosen, or the code ----
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("phone", 50, 56, 3);
		text(hsView == HS_CHOOSE ? "Home server" : "Home server > RomM", true, 17, 170, 62, p.muted, true, 1.4f);
		if (hsView == HS_CHOOSE)
		{
			text("Pair with a server", true, 30, 170, 88, p.ink);
			std::string help = hsSel == 0
				? "RomM, on this network or the internet. You approve this handheld by scanning a QR code with your phone."
				: "Romhost (Homeport), on this network: found by itself, then paired with the code Homeport shows.";
			if (paired) help += " This replaces the pairing with " + homeServer() + ".";
			wrapped(help, 20, 50, 196, 540, 10, p.muted);
		}
		else if (hsView == HS_FORM)
		{
			text("RomM's address", true, 30, 170, 88, p.ink);
			rawText = true;
			if (!hsHost.empty()) text(fit(hsBase(), true, 22, 420), true, 22, 170, 130, p.accent);
			rawText = false;
			int row = rows[hsSel];
			std::string help = !hsMsg.empty() ? hsMsg
				: row == F_PROTO ? "HTTPS for a server on the internet or with a certificate. HTTP for a plain one on this network."
				: row == F_ADDR ? "As in a browser: romm.example.com, 192.168.1.20, or a full URL such as https://example.com:8443/romm."
				: row == F_PORT ? "Only if RomM isn't on the usual port (443 for HTTPS, 80 for HTTP)."
				: row == F_TLS ? "On: the server's certificate is checked, as a browser does. Off: for a self-signed certificate. Keep it On on networks you don't trust. A change holds at once."
				: "Asks " + (hsHost.empty() ? std::string("the address") : hsBase()) + " for RomM, then shows a QR code to approve this handheld from your phone.";
			wrapped(help, 20, 50, 196, 540, 10, hsMsg.empty() ? p.muted : p.ink);
		}
		else
		{
			if (hsDone) text("Paired", true, 30, 170, 88, p.ink);
			else if (!hsCode.empty())
			{
				rawText = true;
				text(hsCode, true, 44, 170, 80, p.ink);
				rawText = false;
			}
			else text(hsName.empty() ? std::string("RomM") : hsName, true, 30, 170, 88, p.ink);
			float y = 196;
			if (!hsMsg.empty()) { wrapped(hsMsg, 20, 50, y, 540, 6, hsStep == 0 && !hsDone ? p.accent : p.ink); y += 150; }
			else if (!hsVerify.empty())
			{
				text("Scan the QR code below with your phone, or open:", false, 18, 50, y, p.muted);
				// (a URL has no spaces to wrap at: broken where the line is full, three lines at most)
				rawText = true;
				std::string rest = hsVerify;
				for (int l = 0; l < 3 && !rest.empty(); l++)
				{
					size_t n = rest.size();
					while (n > 1 && text(rest.substr(0, n), false, 18, 0, 0, p.ink, false, 0, true) > 540) n--;
					text(rest.substr(0, n), false, 18, 50, y + 28 + l * 24, p.ink);
					rest = rest.substr(n);
				}
				rawText = false;
				int left = std::max(0, (int)std::ceil((hsExpires - now) / 1000));
				char t[16];
				snprintf(t, sizeof t, "%d:%02d", left / 60, left % 60);
				text("Waiting for approval in RomM...", true, 20, 50, y + 110, p.ink);
				text(t, true, 20, 590 - text(t, true, 20, 0, 0, p.ink, false, 0, true), y + 110, p.accent);
				y += 150;
			}
			if (!hsNote.empty() && !hsDone) wrapped(hsNote, 18, 50, std::max(y, 360.f), 540, 3, p.muted);
		}
		be->endScreen();

		// ---- bottom: the list, or the QR code ----
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		if (hsView == HS_PAIR)
		{
			if (!hsVerify.empty() && hsStep == 3)
			{
				// the page that approves the handheld, as a QR code: dark on white with its 4-module quiet zone, a whole
				// number of device pixels a module so it stays sharp, as large as the panel above the buttons allows
				if (hsQrFor != hsVerify || !hsQr)
				{
					hsQr = nullptr;
					hsQrFor = hsVerify;
					try
					{
						qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(hsVerify.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
						int n = qr.getSize();
						hsQrSize = n;
						hsQrPx = std::max(1, (int)std::floor(X(372) / (n + 8)));
						rnds::Bitmap b;
						b.w = b.h = n * hsQrPx; b.alpha = true;
						b.px.assign((size_t)b.w * b.h, 0);
						for (int my = 0; my < n; my++)
							for (int mx = 0; mx < n; mx++)
								if (qr.getModule(mx, my))
									for (int yy = my * hsQrPx; yy < (my + 1) * hsQrPx; yy++)
										std::fill(b.px.begin() + (size_t)yy * b.w + mx * hsQrPx, b.px.begin() + (size_t)yy * b.w + (mx + 1) * hsQrPx, (uint8_t)255);
						hsQr = be->upload(b, false);
						fprintf(stderr, "home server: QR code %dx%d modules\n", n, n);
					}
					catch (const std::exception& e) { fprintf(stderr, "home server: no QR code: %s\n", e.what()); }
				}
				if (hsQr)
				{
					float side = (float)(hsQrSize + 8) * hsQrPx;
					float x0 = std::round(X(320) - side / 2), y0 = std::round(X(14));
					be->fill(x0, y0, side, side, Color(1, 1, 1, 1));
					be->draw(hsQr, x0 + 4 * hsQrPx, y0 + 4 * hsQrPx, (float)hsQr->w, (float)hsQr->h, Color(0, 0, 0, 1));
					float caption = 14 + side / s + 6;
					if (caption < 404) { float tw = text("Scan with your phone", true, 17, 0, 0, p.muted, false, 0, true); text("Scan with your phone", true, 17, 320 - tw / 2, caption, p.muted); }
				}
			}
			else
			{
				box("bubble", false, 18, 14, 604, 54);
				text(hsDone ? "Paired" : hsStep == 3 ? "Asking RomM" : "Not paired", true, 22, 38, 28, p.ink);
				std::string what = hsDone ? "A: Sync now syncs the saves. B: back to Home server."
					: hsStep == 3 ? "A moment..." : "A asks RomM for a new code. X pairs with a code from RomM instead. B goes back to the address.";
				wrapped(what, 20, 38, 96, 564, 8, p.muted);
			}
		}
		else
		{
			box("bubble", false, 18, 14, 604, 54);
			text(hsView == HS_CHOOSE ? "Which server?" : "RomM", true, 22, 38, 28, p.ink);
			int n = hsView == HS_CHOOSE ? 2 : (int)rows.size();
			for (int r = 0; r < n && r < ROWS; r++)
			{
				bool on = r == hsSel;
				float y = 82 + r * 64;
				box(on ? "pick" : "slot", on, 18, y, 604, 56);
				Color c = on ? p.onPick : p.muted;
				std::string label, value;
				bool choice = false, warn = false;
				if (hsView == HS_CHOOSE)
				{
					label = r == 0 ? "RomM" : "Romhost (Homeport)";
					value = r == 0 ? (hsBackend() == "romm" ? "Paired" : "Address and QR code") : (hsBackend() == "romhost" ? "Paired" : "On this network");
				}
				else
				{
					int row = rows[r];
					if (row == F_PROTO) { label = "Protocol"; value = hsHttps ? "HTTPS" : "HTTP"; choice = true; }
					else if (row == F_ADDR) { label = "Address"; value = hsHost.empty() ? "Not set" : hsHost + hsPath; warn = hsHost.empty(); }
					else if (row == F_PORT) { label = "Port"; value = hsPort.empty() ? (hsHttps ? "Usual (443)" : "Usual (80)") : hsPort; }
					else if (row == F_TLS) { label = "Check certificate"; value = getConfValue(data + "/settings.conf", "homesync_tls", "on") == "off" ? "Off" : "On"; choice = true; }
					else { label = hsStep == 4 ? "Checking..." : "Connect"; }
				}
				Color vc = warn ? (on ? p.onPickAccent : p.accent) : c;
				rawText = hsView == HS_FORM && r < (int)rows.size() && rows[r] == F_ADDR && !hsHost.empty();
				std::string v = value.empty() ? "" : fit(value, true, 19, choice ? 300 : 330);
				float vw = v.empty() ? 0 : text(v, true, 19, 0, 0, vc, false, 0, true);
				rawText = false;
				float room = (choice ? 556 - vw - 16 : 586 - vw) - 38 - 16;
				text(fit(label, true, 20, room), true, 20, 38, y + 16, c);
				if (v.empty()) continue;
				rawText = hsView == HS_FORM && rows[r] == F_ADDR && !hsHost.empty();
				if (choice)
				{
					arrow(true, 576, y + 21, vc);
					text(v, true, 19, 566 - vw, y + 17, vc);
					arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, vc);
				}
				else text(v, true, 19, 586 - vw, y + 17, vc);
				rawText = false;
			}
		}
		drawChips(homeChips());
		be->endScreen();
	}
}
