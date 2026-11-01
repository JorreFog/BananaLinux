#pragma once
// banana-fetch run: the jobs in banana/downloads, oldest first, one at a time, until none is left. One runner at a
// time: its pid in /run/banana/transfer.pid, which also tells bananad that a closed lid waits for it to end (and that
// the menu doesn't sleep for idleness meanwhile). It runs niced, its disk use only when nothing else wants the card;
// banana-ui stops it (SIGSTOP) while a game runs. A stop (SIGTERM) keeps the parts and the job: the next run goes on
// from there. A job that needs a password nobody remembered waits for one (the status says so) and the runner ends.
// Each job done is said in /run/banana/fetch.finished (its name), after its file became <time>.finished.
#include "paths.h"
#include <string>

namespace banana
{
	namespace fetch
	{
		int runJobs(const Paths& p);
		// a line of banana/logs/downloads.log, with the time (never a password)
		void logLine(const Paths& p, const std::string& s);
	}
}
