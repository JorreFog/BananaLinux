#pragma once
// banana-fetch: the file servers, banana/nas.conf on the card: an [id] section each, key = value lines (read as data,
// never run). A remembered password is kept sealed: AES-256-GCM under a key made (HKDF-SHA256)
// from banana/nas.key, 32 random bytes, and the SoC's serial number, which isn't on the card, so the card read on a
// computer or put in another handheld doesn't give it away (Files asks for it again there). Each password is also
// bound to where it is sent (protocol, host, port, user): moved to another server's section it doesn't open. Someone
// with the handheld itself can still open it: there is no secure store on it.
#include "remote.h"
#include <utility>

namespace banana
{
	namespace fetch
	{
		struct ServerRec : Server
		{
			bool remember = true;		// the password is kept (sealed); otherwise asked each time
			std::string sealed;			// as kept: "v2:..."
			std::vector<std::pair<std::string, std::string>> targets;	// a remote folder -> where its downloads went last
			std::string title() const { return name.empty() ? host : name; }
			std::string target(const std::string& remoteDir) const;
		};

		std::vector<ServerRec> loadServers(const std::string& path);
		bool saveServers(const std::string& path, const std::vector<ServerRec>& list);
		// read, changed by fn (false: nothing to write) and written, under a lock: the session and a running job both
		// change the file
		bool updateServers(const std::string& path, const std::function<bool(std::vector<ServerRec>&)>& fn);
		ServerRec* findServer(std::vector<ServerRec>& list, const std::string& id);
		std::string newId();

		class SecretBox
		{
		public:
			// keyPath: banana/nas.key (made the first time); serial: where the SoC's serial number is read from
			bool open(const std::string& keyPath, const std::string& serial = "/proc/device-tree/serial-number");
			std::string seal(const std::string& plain, const ServerRec& s);
			// false when it doesn't open: another handheld, the key file gone, or the server's address changed
			bool unseal(const std::string& sealed, const ServerRec& s, std::string& plain);
			// the key has the SoC's serial number in it (false: the device tree had none, and nas.key alone opens it)
			bool bound() const { return withSerial; }
		private:
			unsigned char key[32];
			bool ready = false, withSerial = false;
		};
	}
}
