#pragma once
// banana-fetch: a file server, whatever its protocol (SMB, SFTP, FTP; "dir", a local folder, for the tests). Paths are
// absolute and slash-separated ("/Games/NDS"); on SMB the first part is the share, and "/" lists the shares. A Backend
// is one connection used by one thread at a time, but abort() may come from another (the stall watchdog, a stop): it
// makes whatever waits on the network return.
#include "names.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace banana
{
	namespace fetch
	{
		enum class Err { OK, AUTH, HOSTKEY, NOT_FOUND, DENIED, NO_RESUME, NETWORK, TIMEOUT, STOPPED, LOCAL, NO_SPACE, FAT32,
			PASSWORD, WRONG_PASSWORD, UNSAFE, NO_MEMORY, UNSUPPORTED, OTHER };
		// its word in the session's replies and the jobs' reports ("auth", "hostkey"...)
		const char* errWord(Err e);
		Err errFromWord(const std::string& w);
		// worth trying again later (the network), as opposed to what another try won't change (a wrong password...)
		bool transient(Err e);

		struct Result
		{
			Err err = Err::OK;
			std::string msg;		// what went wrong, in words, for the screen and the log
			std::string got;		// HOSTKEY: the key (or certificate) the server showed this time
			Result() {}
			Result(Err e, const std::string& m = "") : err(e), msg(m) {}
			explicit operator bool() const { return err == Err::OK; }
		};

		struct Entry
		{
			std::string name;
			int64_t size = -1;
			bool dir = false;
			bool link = false;		// a link whose target couldn't be told (FTP): a folder if it lists, else a file
			int64_t mtime = 0;		// unix time, 0 when not known
		};

		// what a connection needs: a server of banana/nas.conf, its password opened (servers.cpp)
		struct Server
		{
			std::string id, name, protocol, host, user, password, domain, start, encoding = "utf-8", hostkey;
			int port = 0;
			bool guest = false, tls = false;
			int defaultPort() const { return protocol == "sftp" ? 22 : protocol == "smb" ? 445 : 21; }
			int portOrDefault() const { return port > 0 ? port : defaultPort(); }
		};

		typedef std::function<bool(const char*, size_t)> Sink;

		class Backend
		{
		public:
			virtual ~Backend() {}
			virtual Result connect() = 0;
			virtual Result list(const std::string& dir, std::vector<Entry>& out) = 0;
			// the file's bytes from offset on, in order, to sink; sink returning false stops it (Err::STOPPED)
			virtual Result fetch(const std::string& path, int64_t offset, const Sink& sink) = 0;
			// who the server is, once connected: "ssh-ed25519 SHA256:..." (SFTP), "SHA256:..." (FTPS), "" otherwise
			virtual std::string fingerprint() const { return ""; }
			// where the login lands (FTP's and SFTP's home folder; "/" elsewhere): the start without a start folder
			virtual std::string home() const { return "/"; }
			virtual void abort() = 0;
			// while a session waits for the next request (a server that hangs up an idle connection)
			virtual void keepalive() {}
		};

		// nullptr for a protocol this build doesn't have
		std::unique_ptr<Backend> makeBackend(const Server& s);

		// (cleanPath, joinPath, parentPath, baseName: names.h)
		void sortEntries(std::vector<Entry>& v);		// folders first, then by name without regard to case

		// a TCP connection with a timeout (SFTP's): -1 and why on failure
		int dialTcp(const std::string& host, int port, int timeoutMs, std::string& why);
		// why a connection to host wasn't made (its errno: refused, no answer, no route), in words
		std::string connectError(const std::string& host, int err);
		// a pinned key against the one a server showed ("<type> SHA256:..."; a pin without its type matches any type)
		bool pinMatches(const std::string& pin, const std::string& got);
		std::string base64(const unsigned char* p, size_t n, bool pad = true);
	}
}
