#pragma once
// Downloads' files: on the card (BANANA_DATA, /data/banana) and in /run (BANANA_RUN, /run/banana). banana-ui and
// banana-fetch find them the same way.
#include <cstdlib>
#include <string>

namespace banana
{
	namespace fetch
	{
		struct Paths
		{
			std::string data = "/data/banana", run = "/run/banana";
			std::string servers() const { return data + "/nas.conf"; }
			std::string key() const { return data + "/nas.key"; }
			std::string jobs() const { return data + "/downloads"; }
			std::string log() const { return data + "/logs/downloads.log"; }
			// the item in hand's progress, twice a second at most: in a folder of its own, so that the menu's watch on
			// /run/banana (the launch file) isn't woken by it
			std::string status() const { return run + "/fetch/status"; }
			std::string finished() const { return run + "/fetch.finished"; }	// a job done (its file's name): the menu's pop-up
			std::string pid() const { return run + "/transfer.pid"; }		// bananad: a transfer the closed lid waits for
			// a password typed for this session only (the server doesn't remember it), for the runner, read once
			std::string password(const std::string& serverId) const { return run + "/fetch-" + serverId + ".pw"; }
			static Paths fromEnv(const std::string& dataDir = "")
			{
				Paths p;
				const char* d = getenv("BANANA_DATA");
				const char* r = getenv("BANANA_RUN");
				if (!dataDir.empty()) p.data = dataDir;
				else if (d && *d) p.data = d;
				if (r && *r) p.run = r;
				return p;
			}
		};
	}
}
