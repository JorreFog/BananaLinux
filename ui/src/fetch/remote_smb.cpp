// banana-fetch: SMB2/3 through libsmb2 (NTLMv2, or guest; SMB3 signing and encryption as the server asks). "/" lists
// the server's disk shares (IPC$'s share list); below it a path is "/<share>/<path in the share>". A share's tree is
// a libsmb2 context of its own, connected the first time the share is used, and again after its connection was lost.
// The requests are libsmb2's asynchronous ones, waited on here (Call): its synchronous wrappers (6.2) free what an
// unanswered request still points at when the connection fails, and the context's end then writes into freed memory.
#include "remote.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>

namespace banana
{
	namespace fetch
	{
		namespace
		{
			const int TIMEOUT = 30;		// s: an answer that doesn't come (libsmb2's own timeout, then the wait's)

			// NTSTATUS codes that mean "the login was refused"
			bool loginRefused(uint32_t st)
			{
				switch (st)
				{
				case 0xC000006D: case 0xC000006E: case 0xC0000070: case 0xC0000071: case 0xC0000072:
				case 0xC0000193: case 0xC0000224: case 0xC0000234:
					return true;
				}
				return false;
			}

			// what else a server's answer may say: the file, the folder or the share isn't there; no access; and the
			// session, the share or the link gone, which a new connection gets past (tried again)
			Err answerErr(uint32_t st)
			{
				switch (st)
				{
				case 0xC0000034: case 0xC000003A: case 0xC000000F: case 0xC00000CC: case 0xC0000033: case 0xC000003B:
				case 0xC00000BA: case 0xC0000103: case 0xC0000056:
					return Err::NOT_FOUND;
				case 0xC0000022: case 0xC0000043:
					return Err::DENIED;
				case 0xC00000B5:
					return Err::TIMEOUT;
				case 0xC00000C9: case 0xC0000203: case 0xC000035C: case 0xC0000205: case 0xC000009A: case 0xC00000C4:
				case 0xC000020C: case 0xC000020D: case 0xC0000236: case 0xC000023C: case 0xC000023D: case 0xC0000128:
					return Err::NETWORK;
				}
				return Err::OTHER;
			}

			// libsmb2's words for a connection that wasn't made, and its errno: "Socket connect failed with 111",
			// "Connect failed with errno : Connection refused(111)"; 0 when it isn't one
			int connectErrno(const std::string& e)
			{
				size_t at = e.find("Socket connect failed with ");
				if (at != std::string::npos) return atoi(e.c_str() + at + 27);
				at = e.find("Connect failed with errno");
				size_t open = at == std::string::npos ? at : e.rfind('(');
				return open != std::string::npos && open > at ? atoi(e.c_str() + open + 1) : 0;
			}

			// the server never answered: no such name, the connection refused, nothing at the address
			bool unreached(const std::string& e)
			{
				return connectErrno(e) > 0 || e.find("Can not resolve") != std::string::npos || e.find("no connection exists") != std::string::npos;
			}

			// a request in flight: its answer, or (abandoned) nobody waiting for it any more, which its callback frees
			// (the context's end calls every callback still due)
			struct Call
			{
				bool done = false, abandoned = false;
				int status = 0;
				void* data = nullptr;
				void (*drop)(smb2_context*, void*) = nullptr;	// what frees an answer that came too late
			};

			void answered(smb2_context* c, int status, void* data, void* priv)
			{
				Call* k = (Call*)priv;
				if (k->abandoned)
				{
					if (data && k->drop) k->drop(c, data);
					delete k;
					return;
				}
				k->done = true;
				k->status = status;
				k->data = data;
			}

			// c served until k is answered: false when the connection failed first, or no answer came in time (k is then
			// left to its callback, and the context isn't served again)
			bool wait(smb2_context* c, Call* k)
			{
				time_t until = time(nullptr) + TIMEOUT + 5;
				while (!k->done)
				{
					pollfd p = { smb2_get_fd(c), (short)smb2_which_events(c), 0 };
					int n = poll(&p, 1, 1000);
					bool failed = n < 0 && errno != EINTR;
					if (failed) smb2_set_error(c, "Poll failed");
					// (also with nothing to read: libsmb2's timeouts are checked there)
					else if (smb2_service(c, n > 0 ? p.revents : 0) < 0) failed = true;
					if (k->done) break;
					if (!failed && time(nullptr) > until) { smb2_set_error(c, "Timeout expired and no answer came"); failed = true; }
					if (failed) { k->abandoned = true; return false; }
				}
				return true;
			}

			class SmbBackend : public Backend
			{
			public:
				// a guest over SMB 3.1.1: libsmb2 6.2 signs the requests after the login with a key the server (Samba) has
				// none of for a guest, and the share is refused. 3.0.2 doesn't sign them; a server without 3.0.2 (Windows 8
				// and older, an old NAS) is asked for SMB 2 instead.
				explicit SmbBackend(const Server& srv) : s(srv), dialect(srv.guest ? SMB2_VERSION_0302 : SMB2_VERSION_ANY) {}
				~SmbBackend() override { for (auto& t : trees) release(t); }

				Result connect() override
				{
					Result r;
					tree("IPC$", r);	// the login, and the tree the shares' list comes through
					return r;
				}

				Result list(const std::string& dir, std::vector<Entry>& out) override
				{
					std::string share, sub;
					split(dir, share, sub);
					if (share.empty()) return shares(out);
					Result r;
					smb2_context* c = tree(share, r);
					if (!c) return r;
					int st = 0;
					void* data = nullptr;
					r = call(c, "Can't list " + dir, [&](Call* k) { return smb2_opendir_async(c, sub.c_str(), answered, k); }, st, data,
						[](smb2_context* x, void* d) { smb2_closedir(x, (smb2dir*)d); });
					if (!r) return r;
					smb2dir* d = (smb2dir*)data;
					if (!d) return fail(c, "Can't list " + dir, false, st);
					while (smb2dirent* e = smb2_readdir(c, d))
					{
						if (!strcmp(e->name, ".") || !strcmp(e->name, "..")) continue;
						Entry x;
						x.name = e->name;
						x.dir = e->st.smb2_type == SMB2_TYPE_DIRECTORY;
						x.link = e->st.smb2_type == SMB2_TYPE_LINK;	// a reparse point: tried as a folder
						x.size = x.dir ? 0 : (int64_t)e->st.smb2_size;
						x.mtime = (int64_t)e->st.smb2_mtime;
						out.push_back(x);
					}
					smb2_closedir(c, d);
					return Result();
				}

				Result fetch(const std::string& path, int64_t offset, const Sink& sink) override
				{
					std::string share, sub;
					split(path, share, sub);
					if (share.empty() || sub.empty()) return Result(Err::NOT_FOUND, path + " is not a file.");
					Result r;
					smb2_context* c = tree(share, r);
					if (!c) return r;
					int st = 0;
					void* data = nullptr;
					r = call(c, "Can't open " + path, [&](Call* k) { return smb2_open_async(c, sub.c_str(), O_RDONLY, answered, k); }, st, data);
					if (!r) return r;
					smb2fh* fh = (smb2fh*)data;
					if (!fh) return fail(c, "Can't open " + path, false, st);
					uint32_t chunk = std::min<uint32_t>(smb2_get_max_read_size(c), 1u << 20);
					if (chunk == 0) chunk = 64 * 1024;
					std::vector<uint8_t> buf(chunk);
					for (;;)
					{
						r = call(c, "Reading " + path, [&](Call* k) { return smb2_pread_async(c, fh, buf.data(), chunk, (uint64_t)offset, answered, k); },
							st, data);
						if (!r) break;
						if (st < 0) { r = fail(c, "Reading " + path, false, st); break; }
						if (st == 0) break;
						offset += st;
						if (!sink((const char*)buf.data(), (size_t)st)) { r = Result(Err::STOPPED, "Stopped."); break; }
					}
					// (closed on the server while the connection is there; a lost one takes the handle with its context)
					if (!lost(c)) call(c, "Closing " + path, [&](Call* k) { return smb2_close_async(c, fh, answered, k); }, st, data);
					return r;
				}

				void abort() override
				{
					std::lock_guard<std::mutex> l(mu);
					aborted = true;
					for (auto& t : trees)
					{
						int fd = smb2_get_fd(t.ctx);
						if (fd >= 0) shutdown(fd, SHUT_RDWR);
					}
				}

				void keepalive() override
				{
					for (auto& t : trees) if (!t.dead) smb2_echo(t.ctx);
				}

			private:
				struct Tree { std::string share; smb2_context* ctx; bool dead; };
				Server s;
				smb2_negotiate_version dialect;	// what the logins ask for (a guest's: see the constructor)
				std::vector<Tree> trees;
				std::mutex mu;		// trees, against abort() from another thread
				std::atomic<bool> aborted{ false };

				// "/share/a/b" -> "share", "a/b"
				static void split(const std::string& p, std::string& share, std::string& sub)
				{
					std::string c = cleanPath(p).substr(1);
					size_t slash = c.find('/');
					share = c.substr(0, slash);
					sub = slash == std::string::npos ? "" : c.substr(slash + 1);
				}

				// a request: started (start(k) with answered and k), then waited on. A failure to start, or the connection
				// failing before the answer, is r; else status and data are the answer's (status < 0: -errno)
				template <class Start> Result call(smb2_context* c, const std::string& what, Start start, int& status, void*& data,
					void (*drop)(smb2_context*, void*) = nullptr)
				{
					smb2_set_error(c, "");	// (libsmb2 keeps the last answer's status until another comes)
					Call* k = new Call();
					k->drop = drop;
					int rc = start(k);
					if (rc != 0) { delete k; return fail(c, what, false, rc < 0 ? rc : -EIO); }
					if (!wait(c, k)) return fail(c, what, true, 0);
					status = k->status;
					data = k->data;
					delete k;
					return Result();
				}

				bool lost(smb2_context* c)
				{
					std::lock_guard<std::mutex> l(mu);
					for (auto& t : trees) if (t.ctx == c) return t.dead;
					return true;
				}

				// what went wrong, in words. failed: the connection (or no answer in time); else rc is libsmb2's -errno, and
				// the server's status, when it answered, the context's. A connection that failed leaves every tree to be
				// connected again (the server or the link is gone), a session or share the server let go of its own.
				Result fail(smb2_context* c, const std::string& what, bool failed, int rc)
				{
					uint32_t st = (uint32_t)smb2_get_nterror(c);
					const char* got = smb2_get_error(c);
					std::string e = got ? got : "";
					while (!e.empty() && strchr("\n .", e.back())) e.pop_back();
					Result r = describe(what, failed, rc, st, e);
					{
						std::lock_guard<std::mutex> l(mu);
						for (auto& t : trees) if (failed || (t.ctx == c && transient(r.err))) t.dead = true;
					}
					if (aborted) return Result(Err::NETWORK, "The connection was dropped.");
					return r;
				}

				Result describe(const std::string& what, bool failed, int rc, uint32_t st, const std::string& e)
				{
					if (st == 0)
					{
						// not the server's answer: the connection itself (as dialTcp words it), which another try may get back
						int n = connectErrno(e);
						if (n > 0) return Result(n == ETIMEDOUT ? Err::TIMEOUT : Err::NETWORK, connectError(s.host, n));
						if (e.find("Can not resolve") != std::string::npos) return Result(Err::NETWORK, "No server named " + s.host + " was found.");
						if (e.find("no connection exists") != std::string::npos) return Result(Err::TIMEOUT, s.host + " didn't answer in time.");
						if (e.find("Timeout expired") != std::string::npos) return Result(Err::TIMEOUT, what + ": the server didn't answer in time.");
						if (failed)
							return Result(Err::NETWORK, what + ": the connection was lost" +
								(e.find("remote closed") != std::string::npos ? " (the server closed it)." : "."));
						switch (-rc)
						{
						case ENOENT: return Result(Err::NOT_FOUND, what + ": it isn't there.");
						case EACCES: case EPERM: return Result(Err::DENIED, what + ": access denied" + (e.empty() ? "." : " (" + e + ")."));
						case ETIMEDOUT: return Result(Err::TIMEOUT, what + ": the server didn't answer in time.");
						}
						return Result(Err::OTHER, what + (e.empty() ? ": " + std::string(strerror(-rc)) + "." : ": " + e + "."));
					}
					if (loginRefused(st)) return Result(Err::AUTH, "The server refused the name or the password.");
					std::string name = nterror_to_str(st);
					switch (answerErr(st))
					{
					case Err::NOT_FOUND: return Result(Err::NOT_FOUND, what + ": it isn't there (" + name + ").");
					case Err::DENIED: return Result(Err::DENIED, what + (st == 0xC0000043 ? ": it is in use on the server." : ": access denied."));
					case Err::TIMEOUT: return Result(Err::TIMEOUT, what + ": the server didn't answer in time.");
					case Err::NETWORK: return Result(Err::NETWORK, what + ": the server let the connection go (" + name + ").");
					default: return Result(Err::OTHER, what + ": the server said " + name + ".");
					}
				}

				// a tree let go of: logged off while its connection is there (a lost one isn't waited on)
				static void release(const Tree& t)
				{
					if (!t.dead) smb2_disconnect_share(t.ctx);
					smb2_destroy_context(t.ctx);
				}

				// the share's tree, connected the first time, and again once its connection was lost
				smb2_context* tree(const std::string& share, Result& r)
				{
					std::string key = lower(share);
					for (size_t i = 0; i < trees.size(); i++)
					{
						if (trees[i].share != key) continue;
						if (!trees[i].dead) return trees[i].ctx;
						Tree t = trees[i];
						{
							std::lock_guard<std::mutex> l(mu);
							trees.erase(trees.begin() + (long)i);
						}
						release(t);
						break;
					}
					bool away = false;
					smb2_context* c = open(key, share, dialect, r, away);
					if (!c && dialect == SMB2_VERSION_0302 && !away && r.err != Err::AUTH && r.err != Err::NOT_FOUND)
					{
						c = open(key, share, SMB2_VERSION_ANY2, r, away);
						if (c) dialect = SMB2_VERSION_ANY2;	// (the server's other shares the same)
					}
					return c;
				}

				// a context logged in to the server and connected to the share (the server's name with its port when it
				// isn't 445); nullptr and r why not (away: the server never answered)
				smb2_context* open(const std::string& key, const std::string& share, smb2_negotiate_version version, Result& r, bool& away)
				{
					smb2_context* c = smb2_init_context();
					if (!c) { r = Result(Err::OTHER, "No memory for SMB."); return nullptr; }
					smb2_set_version(c, version);
					smb2_set_security_mode(c, SMB2_NEGOTIATE_SIGNING_ENABLED);
					smb2_set_timeout(c, TIMEOUT);
					// guest: Samba maps an unknown user to its guest account ("map to guest = bad user"); Windows and macOS take
					// Guest with no password
					std::string user = s.guest ? "guest" : s.user, pass = s.guest ? "" : s.password;
					smb2_set_user(c, user.c_str());
					smb2_set_password(c, pass.c_str());
					if (!s.domain.empty()) smb2_set_domain(c, s.domain.c_str());
					smb2_set_workstation(c, "BANANA");
					std::string server = s.host.find(':') != std::string::npos && s.host[0] != '[' ? "[" + s.host + "]" : s.host;
					if (s.portOrDefault() != 445) server += ":" + std::to_string(s.portOrDefault());
					{
						std::lock_guard<std::mutex> l(mu);
						trees.push_back({ key, c, false });
					}
					// (its synchronous call keeps what it waits with in the context: nothing freed under a request)
					int rc = smb2_connect_share(c, server.c_str(), share.c_str(), user.c_str());
					if (rc == 0) { r = Result(); return c; }	// (a second try: the first one's error is no more)
					const char* e = smb2_get_error(c);
					away = smb2_get_nterror(c) == 0 && unreached(e ? e : "");
					r = fail(c, share == "IPC$" ? "Connecting to " + s.host : "Opening the share " + share, smb2_get_nterror(c) == 0 && rc == -1, rc);
					{
						std::lock_guard<std::mutex> l(mu);
						trees.pop_back();
					}
					smb2_destroy_context(c);
					return nullptr;
				}

				// the disk shares (not the hidden ones: printers, IPC$, ADMIN$, C$...)
				Result shares(std::vector<Entry>& out)
				{
					Result r;
					smb2_context* c = tree("IPC$", r);
					if (!c) return r;
					// (a server that won't say, its srvsvc pipe off: the share's name goes in the start folder instead)
					const std::string what = "The server didn't list its shares";
					const std::string hint = " Put the share's name in the server's Start folder (/Games, for a share called Games).";
					int st = 0;
					void* data = nullptr;
					r = call(c, what, [&](Call* k) { return smb2_share_enum_async(c, SHARE_INFO_1, answered, k); }, st, data,
						[](smb2_context* x, void* d) { smb2_free_data(x, d); });
					if (r && (st != 0 || !data)) r = fail(c, what, false, st ? st : -EIO);
					if (!r)
					{
						// (no list to be had there, its srvsvc pipe off: nothing of the server itself is missing)
						if (r.err == Err::NOT_FOUND) { r.err = Err::OTHER; r.msg = what + "."; }
						if (!transient(r.err)) r.msg += hint;
						return r;
					}
					auto* rep = (srvsvc_NetrShareEnum_rep*)data;
					for (uint32_t i = 0; i < rep->ses.ShareInfo.Level1.EntriesRead; i++)
					{
						const auto& si = rep->ses.ShareInfo.Level1.Buffer->share_info_1[i];
						std::string name = si.netname.utf8 ? si.netname.utf8 : "";
						if ((si.type & 3) != SHARE_TYPE_DISKTREE || (si.type & SHARE_TYPE_HIDDEN) || name.empty() || name.back() == '$') continue;
						Entry e;
						e.name = name;
						e.dir = true;
						e.size = 0;
						out.push_back(e);
					}
					smb2_free_data(c, rep);
					return Result();
				}
			};
		}

		std::unique_ptr<Backend> makeSmb(const Server& s) { return std::unique_ptr<Backend>(new SmbBackend(s)); }
	}
}
