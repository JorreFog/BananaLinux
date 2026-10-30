// banana-fetch: SFTP through libssh2. The host key is pinned on first use as OpenSSH writes it ("ssh-ed25519
// SHA256:..."); with a pin, the server is asked for a key of the pinned type, so another of its keys can't pass for a
// change. The login is the password, also given to keyboard-interactive's questions.
#include "remote.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <sys/socket.h>
#include <unistd.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

namespace banana
{
	namespace fetch
	{
		namespace
		{
			const char* keyType(int t)
			{
				switch (t)
				{
				case LIBSSH2_HOSTKEY_TYPE_RSA: return "ssh-rsa";
				case LIBSSH2_HOSTKEY_TYPE_DSS: return "ssh-dss";
				case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return "ecdsa-sha2-nistp256";
				case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return "ecdsa-sha2-nistp384";
				case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return "ecdsa-sha2-nistp521";
				case LIBSSH2_HOSTKEY_TYPE_ED25519: return "ssh-ed25519";
				}
				return "unknown";
			}

			class SftpBackend : public Backend
			{
			public:
				explicit SftpBackend(const Server& srv) : s(srv)
				{
					static std::once_flag once;
					std::call_once(once, [] { libssh2_init(0); });
				}
				~SftpBackend() override { drop(); }

				Result connect() override
				{
					std::string pinType = s.hostkey.find(' ') != std::string::npos ? s.hostkey.substr(0, s.hostkey.find(' ')) : "";
					Result r = open(pinType);
					// no key of the pinned type any more: any, so the change shows as a key that changed
					if (!r && r.err == Err::OTHER && !pinType.empty() && kexFailed) r = open("");
					if (!r) return r;
					size_t len = 0;
					int type = 0;
					const char* key = libssh2_session_hostkey(ses, &len, &type);
					const char* hash = libssh2_hostkey_hash(ses, LIBSSH2_HOSTKEY_HASH_SHA256);
					if (!key || !hash) return Result(Err::OTHER, "The server sent no host key.");
					fp = std::string(keyType(type)) + " SHA256:" + base64((const unsigned char*)hash, 32, false);
					if (!pinMatches(s.hostkey, fp))
					{
						Result m(Err::HOSTKEY, "The server's key isn't the one it had before.");
						m.got = fp;
						return m;
					}
					// the login: the password, or keyboard-interactive answered with it
					std::string user = s.user.empty() ? "anonymous" : s.user;
					const char* methods = libssh2_userauth_list(ses, user.c_str(), (unsigned)user.size());
					if (!libssh2_userauth_authenticated(ses))
					{
						std::string m = methods ? methods : "";
						if (m.find("password") != std::string::npos)
							libssh2_userauth_password_ex(ses, user.c_str(), (unsigned)user.size(), s.password.c_str(), (unsigned)s.password.size(), nullptr);
						if (!libssh2_userauth_authenticated(ses) && m.find("keyboard-interactive") != std::string::npos)
						{
							*libssh2_session_abstract(ses) = this;
							libssh2_userauth_keyboard_interactive_ex(ses, user.c_str(), (unsigned)user.size(), answer);
						}
						if (!libssh2_userauth_authenticated(ses))
						{
							if (m.find("password") == std::string::npos && m.find("keyboard-interactive") == std::string::npos)
								return Result(Err::AUTH, "The server takes no passwords (" + m + "): SSH keys are not supported.");
							return Result(Err::AUTH, "The server refused the name or the password.");
						}
					}
					sftp = libssh2_sftp_init(ses);
					if (!sftp) return error("Starting SFTP");
					libssh2_keepalive_config(ses, 1, 30);
					char here[4096];
					int n = libssh2_sftp_realpath(sftp, ".", here, sizeof here - 1);
					if (n > 0) homeDir = cleanPath(std::string(here, (size_t)n));
					return Result();
				}

				Result list(const std::string& dir, std::vector<Entry>& out) override
				{
					std::string d = cleanPath(dir);
					LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_opendir(sftp, d.c_str());
					if (!h) return error("Can't list " + d);
					char name[4096], longentry[4096];
					LIBSSH2_SFTP_ATTRIBUTES a;
					for (;;)
					{
						int n = libssh2_sftp_readdir_ex(h, name, sizeof name, longentry, sizeof longentry, &a);
						if (n < 0) { libssh2_sftp_closedir(h); return error("Listing " + d); }
						if (n == 0) break;
						std::string nm(name, (size_t)n);
						if (nm == "." || nm == "..") continue;
						Entry e;
						e.name = nm;
						bool perm = a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS;
						if (perm && LIBSSH2_SFTP_S_ISLNK(a.permissions))
						{
							// a link: what it points at decides, and one pointing nowhere is left out
							LIBSSH2_SFTP_ATTRIBUTES t;
							if (libssh2_sftp_stat(sftp, joinPath(d, nm).c_str(), &t) != 0) continue;
							a = t;
						}
						e.dir = (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(a.permissions);
						e.size = e.dir ? 0 : (a.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (int64_t)a.filesize : -1;
						e.mtime = (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? (int64_t)a.mtime : 0;
						out.push_back(e);
					}
					libssh2_sftp_closedir(h);
					return Result();
				}

				Result fetch(const std::string& path, int64_t offset, const Sink& sink) override
				{
					std::string p = cleanPath(path);
					LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(sftp, p.c_str(), LIBSSH2_FXF_READ, 0);
					if (!h) return error("Can't open " + p);
					libssh2_sftp_seek64(h, (libssh2_uint64_t)offset);
					std::vector<char> buf(256 * 1024);
					Result r;
					for (;;)
					{
						ssize_t n = libssh2_sftp_read(h, buf.data(), buf.size());
						if (n < 0) { r = error("Reading " + p); break; }
						if (n == 0) break;
						if (!sink(buf.data(), (size_t)n)) { r = Result(Err::STOPPED, "Stopped."); break; }
					}
					libssh2_sftp_close(h);
					return r;
				}

				std::string fingerprint() const override { return fp; }
				std::string home() const override { return homeDir; }

				void abort() override
				{
					aborted = true;
					int fd = sock;
					if (fd >= 0) shutdown(fd, SHUT_RDWR);
				}

				void keepalive() override
				{
					int next = 0;
					if (ses) libssh2_keepalive_send(ses, &next);
				}

			private:
				Server s;
				std::atomic<int> sock{ -1 };
				std::atomic<bool> aborted{ false };
				LIBSSH2_SESSION* ses = nullptr;
				LIBSSH2_SFTP* sftp = nullptr;
				std::string fp, homeDir = "/";
				bool kexFailed = false;

				static void answer(const char*, int, const char*, int, int n, const LIBSSH2_USERAUTH_KBDINT_PROMPT*,
					LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract)
				{
					SftpBackend* me = (SftpBackend*)*abstract;
					for (int i = 0; i < n; i++)
					{
						responses[i].text = strdup(me->s.password.c_str());
						responses[i].length = (unsigned)me->s.password.size();
					}
				}

				void drop()
				{
					if (sftp) libssh2_sftp_shutdown(sftp);
					if (ses)
					{
						libssh2_session_disconnect(ses, "bye");
						libssh2_session_free(ses);
					}
					if (sock >= 0) close(sock);
					sftp = nullptr;
					ses = nullptr;
					sock = -1;
				}

				// the TCP connection and the SSH handshake, asking for a host key of type pinType when it is set
				Result open(const std::string& pinType)
				{
					drop();
					kexFailed = false;
					std::string why;
					int fd = dialTcp(s.host, s.portOrDefault(), 10000, why);
					if (fd < 0) return Result(Err::NETWORK, why);
					sock = fd;
					ses = libssh2_session_init();
					if (!ses) return Result(Err::OTHER, "No memory for SSH.");
					libssh2_session_set_blocking(ses, 1);
					libssh2_session_set_timeout(ses, 30000);
					if (!pinType.empty())
					{
						std::string prefs = pinType == "ssh-rsa" ? "rsa-sha2-512,rsa-sha2-256,ssh-rsa" : pinType;
						libssh2_session_method_pref(ses, LIBSSH2_METHOD_HOSTKEY, prefs.c_str());
					}
					int rc = libssh2_session_handshake(ses, fd);
					if (rc != 0)
					{
						kexFailed = rc == LIBSSH2_ERROR_KEX_FAILURE;
						Result r = error("The SSH handshake");
						if (kexFailed) r.err = Err::OTHER;
						return r;
					}
					return Result();
				}

				// the last error of the session (and of SFTP), in words
				Result error(const std::string& what)
				{
					char* msg = nullptr;
					int rc = ses ? libssh2_session_last_error(ses, &msg, nullptr, 0) : 0;
					std::string why = msg && *msg ? msg : "error " + std::to_string(rc);
					if (aborted) return Result(Err::NETWORK, "The connection was dropped.");
					if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL && sftp)
					{
						unsigned long fx = libssh2_sftp_last_error(sftp);
						if (fx == LIBSSH2_FX_NO_SUCH_FILE || fx == LIBSSH2_FX_NO_SUCH_PATH) return Result(Err::NOT_FOUND, what + ": it isn't there.");
						if (fx == LIBSSH2_FX_PERMISSION_DENIED) return Result(Err::DENIED, what + ": permission denied.");
						return Result(Err::OTHER, what + ": the server said " + std::to_string(fx) + ".");
					}
					if (rc == LIBSSH2_ERROR_TIMEOUT) return Result(Err::TIMEOUT, what + ": the server didn't answer in time.");
					if (rc == LIBSSH2_ERROR_SOCKET_RECV || rc == LIBSSH2_ERROR_SOCKET_SEND || rc == LIBSSH2_ERROR_SOCKET_DISCONNECT ||
						rc == LIBSSH2_ERROR_SOCKET_NONE || rc == LIBSSH2_ERROR_CHANNEL_CLOSED || rc == LIBSSH2_ERROR_CHANNEL_EOF_SENT)
						return Result(Err::NETWORK, what + ": the connection was lost (" + why + ").");
					return Result(Err::OTHER, what + ": " + why);
				}
			};
		}

		std::unique_ptr<Backend> makeSftp(const Server& s) { return std::unique_ptr<Backend>(new SftpBackend(s)); }
	}
}
