#pragma once
// What the save sync keeps on the card so that no save is ever lost to it: the vault, a copy of every save it
// replaced (banana/save-vault/<the save's folder>/<its stem>/<UTC time>-<MD5's first 8>.<ext>, the same bytes kept
// once, never pruned by the sync) with the newest real save of each game as last-good.<ext>; and the journal
// (banana/logs/sync/journal.log), a line before and after each operation, so a run cut short is checked on the next.
// The vault is outside saves/ and states/, so nothing in it is ever synced.
#include <string>

namespace bsync
{
	std::string vaultDir();
	std::string journalPath();
	// a copy of src kept as the save at rel (data-relative) was: written, synced, read back and compared. kept: its path;
	// added: false when the vault had those bytes already
	bool vaultKeep(const std::string& rel, const std::string& src, std::string& kept, std::string& err, bool* added = nullptr);
	// data (the save at rel, as just installed or sent) as rel's last-good copy, unless it is blank
	bool vaultGood(const std::string& rel, const std::string& data);

	// a downloaded save (dl, checked, on /run) put in place as the save at rel: the save there now kept in the vault
	// first; journalled; written beside, renamed, read back; then the last good copy. dl is removed when installed.
	// Blank (a core's fresh save on the server): not taken, and not an error either; err says why when Failed
	enum class Install { Done, Blank, Failed };
	Install installSave(const std::string& rel, const std::string& dl, const std::string& serverId, std::string& err);

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
	// each begin with no end (a run killed, the power lost): the save's MD5 says how far it got, the before value
	// (unchanged), the one wanted (installed) or neither (changed-since: played since, kept as it is); a .new left
	// beside it removed. Returns "<result> <rel>" for each
	std::string journalRecover();
}
