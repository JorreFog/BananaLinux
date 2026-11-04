#pragma once
// What the save sync keeps on the card so that no save is ever lost to it: the vault, a copy of every save it
// replaced (banana/save-vault/<the save's folder>/<its stem>/<UTC time>-<MD5's first 8>.<ext>, the same bytes kept
// once, never pruned by the sync) with the newest real save of each game as last-good.<ext>; and the journal
// (banana/logs/sync/journal.log), a line before and after each operation, so a run cut short is checked on the next.
// The vault is outside saves/ and states/, so nothing in it is ever synced.
#include <functional>
#include <string>
#include <vector>

namespace bsync
{
	std::string vaultDir();
	std::string journalPath();
	// a copy of src kept as the save at rel (data-relative) was: written, synced, read back and compared. kept: its path;
	// added: false when the vault had those bytes already
	bool vaultKeep(const std::string& rel, const std::string& src, std::string& kept, std::string& err, bool* added = nullptr);
	// data (the save at rel, as just installed or sent) as rel's last-good copy, unless it is blank
	bool vaultGood(const std::string& rel, const std::string& data);
	// what the vault holds, each file with the save it is a copy of (data-relative) and its size, by path; an unfinished
	// copy not listed
	struct Kept { std::string rel, file; long long size; };
	std::vector<Kept> vaultList();
	// path, once links and .. are followed, is a file in the vault: the whole path then; "" when it isn't one
	std::string vaultFile(const std::string& path);

	// a downloaded save (dl, checked, on /run) put in place as the save at rel: the save there now kept in the vault
	// first; journalled; the quick-resume states that belong with the old save removed; written beside, renamed, read
	// back; then the last good copy. dl is removed when installed. Blank (a core's fresh save on the server): not taken,
	// and not an error either; err says why when Failed. expect: the MD5 the save at rel had when the decision to
	// replace it was made ("" for none), so one written since is left. serverHash: the server's MD5 for the version
	// when the card takes it in another form (a .dsv's raw save), for the journal; "" when it is the save's own.
	// openId: given, a Done leaves its journal entry open, its id there ("" when nothing was written), for the caller
	// to end once the agreement is recorded (an OpenOp does), so a run killed between the two is agreed at the next start
	enum class Install { Done, Blank, Failed };
	Install installSave(const std::string& rel, const std::string& dl, const std::string& serverId, std::string& err, const std::string* expect = nullptr, const std::string& serverHash = "", std::string* openId = nullptr);

	struct JOp
	{
		std::string op, rel;	// what ("install", the one operation so far), and the save's data-relative path
		std::string before;		// the save's MD5 before ("" when there was none)
		std::string serverId, serverHash, vault;
		std::string want;		// the MD5 the save has once the operation is done
	};
	// the begin line, synced; the operation's id ("": not written, so the operation must not run)
	std::string journalBegin(const JOp& o);
	bool journalEnd(const std::string& id, const std::string& result, const std::string& after);
	// an install's journal entry left open by installSave, ended "ok" (the save as it was put in place) when close()d
	// or at the end of its scope, whichever is first
	struct OpenOp
	{
		std::string id, md5;
		void close() { if (!id.empty()) journalEnd(id, "ok", md5); id.clear(); }
		~OpenOp() { close(); }
	};
	// each begin with no end (a run killed, the power lost): the save's MD5 says how far it got, the before value
	// (unchanged), the one wanted (installed) or neither (changed-since: played since, kept as it is); the new file
	// still beside the save's name with nothing under it (the power going in the rename) is put in place; a .new left
	// beside it otherwise removed. One installed is agreed in c's record for it (the run was killed before it could), so
	// the next sync doesn't take the save for the card's own change; persist writes c back, and only then is the
	// journal's operation closed, so a cut between the two is recovered again. Returns "<result> <rel>" for each
	struct Conf;
	std::string journalRecover(Conf* c = nullptr, const std::function<bool()>& persist = nullptr);
}
