// banana-fetch: FTP and FTPS (explicit TLS) through libcurl, passive only (EPSV, then PASV). A folder is listed with
// MLSD, or with LIST and libcurl's own parser of it (its wildcard matching) on a server without MLSD; a download
// resumes with REST. Paths are absolute from the server's root ("%2F", and libcurl's NOCWD: the commands carry the
// whole path). An FTPS server's certificate is pinned on first use, its SHA-256 checked in OpenSSL's verify callback,
// so before the password is sent. An old server's CP1251 or CP866 names (the server's Encoding) are translated both
// ways.
#include "remote.h"
#include "charsets.h"
#include <atomic>
#include <cstring>
#include <ctime>
#include <mutex>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace banana
{
	namespace fetch
	{
		namespace
		{
			size_t discard(char*, size_t sz, size_t n, void*) { return sz * n; }
			size_t toString(char* p, size_t sz, size_t n, void* u) { ((std::string*)u)->append(p, sz * n); return sz * n; }

			// a line of MLSD: "type=file;size=1234;modify=20240102030405; name"
			bool mlsdLine(std::string ln, Entry& e)
			{
				while (!ln.empty() && (ln.back() == '\r' || ln.back() == '\n')) ln.pop_back();
				size_t sp = ln.find(' ');
				if (sp == std::string::npos) return false;
				e.name = ln.substr(sp + 1);
				std::string facts = ln.substr(0, sp);
				bool skip = false;
				size_t a = 0;
				while (a < facts.size())
				{
					size_t b = facts.find(';', a);
					if (b == std::string::npos) b = facts.size();
					std::string f = facts.substr(a, b - a);
					a = b + 1;
					size_t eq = f.find('=');
					if (eq == std::string::npos) continue;
					std::string k = f.substr(0, eq), v = f.substr(eq + 1);
					for (auto& c : k) c = (char)tolower((unsigned char)c);
					std::string lv = v;
					for (auto& c : lv) c = (char)tolower((unsigned char)c);
					if (k == "type")
					{
						if (lv == "cdir" || lv == "pdir") skip = true;
						e.dir = lv == "dir";
						e.link = lv.find("slink") != std::string::npos || lv.find("symlink") != std::string::npos;
					}
					else if (k == "size" || k == "sizd") e.size = atoll(v.c_str());
					else if (k == "modify" && v.size() >= 14)
					{
						struct tm t = {};
						t.tm_year = atoi(v.substr(0, 4).c_str()) - 1900;
						t.tm_mon = atoi(v.substr(4, 2).c_str()) - 1;
						t.tm_mday = atoi(v.substr(6, 2).c_str());
						t.tm_hour = atoi(v.substr(8, 2).c_str());
						t.tm_min = atoi(v.substr(10, 2).c_str());
						t.tm_sec = atoi(v.substr(12, 2).c_str());
						e.mtime = (int64_t)timegm(&t);
					}
				}
				if (e.dir) e.size = 0;
				return !skip && !e.name.empty() && e.name != "." && e.name != "..";
			}

			class FtpBackend : public Backend
			{
			public:
				explicit FtpBackend(const Server& srv) : s(srv), table(charset::byName(srv.encoding))
				{
					static std::once_flag once;
					std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
					h = curl_easy_init();
					if (!table) quote = curl_slist_append(nullptr, "*OPTS UTF8 ON");	// (a server that has no such command: ignored)
				}
				~FtpBackend() override
				{
					if (h) curl_easy_cleanup(h);
					if (quote) curl_slist_free_all(quote);
				}

				Result connect() override
				{
					// the login (FTPS: the handshake first, the certificate checked), nothing listed
					if (!h) return Result(Err::OTHER, "No memory for FTP.");
					prepare(url("/", true));
					// (a libcurl on another TLS library than OpenSSL couldn't check the pin: no FTPS rather than an unchecked one)
					if (s.tls && !pinnable) return Result(Err::UNSUPPORTED, "FTPS needs libcurl with OpenSSL, which this build doesn't have.");
					curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
					Result r = perform("Connecting to " + s.host);
					// the folder the login lands in (a server that doesn't lock the account in its folder: "/" is its disk)
					char* entry = nullptr;
					if (r && curl_easy_getinfo(h, CURLINFO_FTP_ENTRY_PATH, &entry) == CURLE_OK && entry && *entry)
						homeDir = cleanPath(charset::toUtf8(entry, table));
					return r;
				}

				Result list(const std::string& dir, std::vector<Entry>& out) override
				{
					std::string d = cleanPath(dir);
					if (!noMlsd)
					{
						std::string body;
						prepare(url(d, true));
						curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "MLSD");
						curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, toString);
						curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
						Result r = perform("Listing " + d);
						if (r)
						{
							size_t a = 0;
							while (a < body.size())
							{
								size_t b = body.find('\n', a);
								if (b == std::string::npos) b = body.size();
								Entry e;
								if (mlsdLine(body.substr(a, b - a), e)) { e.name = charset::toUtf8(e.name, table); out.push_back(e); }
								a = b + 1;
							}
							return r;
						}
						// a server without MLSD: LIST from now on
						if (code != 500 && code != 501 && code != 502 && code != 504) return r;
						noMlsd = true;
					}
					struct Collect { std::vector<Entry>* out; const uint16_t* table; } c = { &out, table };
					auto begin = [](const void* info, void* ptr, int) -> long
					{
						const curl_fileinfo* fi = (const curl_fileinfo*)info;
						Collect* c = (Collect*)ptr;
						std::string name = fi->filename ? fi->filename : "";
						if (name.empty() || name == "." || name == "..") return CURL_CHUNK_BGN_FUNC_SKIP;
						Entry e;
						e.name = charset::toUtf8(name, c->table);
						e.dir = fi->filetype == CURLFILETYPE_DIRECTORY;
						e.link = fi->filetype == CURLFILETYPE_SYMLINK;
						e.size = e.dir ? 0 : (fi->flags & CURLFINFOFLAG_KNOWN_SIZE) ? (int64_t)fi->size : -1;
						e.mtime = (fi->flags & CURLFINFOFLAG_KNOWN_TIME) ? (int64_t)fi->time : 0;
						c->out->push_back(e);
						return CURL_CHUNK_BGN_FUNC_SKIP;	// listed, not downloaded
					};
					prepare(url(d, true) + "*");
					curl_easy_setopt(h, CURLOPT_WILDCARDMATCH, 1L);
					curl_easy_setopt(h, CURLOPT_CHUNK_BGN_FUNCTION, (curl_chunk_bgn_callback)begin);
					curl_easy_setopt(h, CURLOPT_CHUNK_DATA, &c);
					Result r = perform("Listing " + d);
					// (an empty folder matches nothing: libcurl says the file isn't there)
					if (r.err == Err::NOT_FOUND && out.empty() && code != 550) return Result();
					return r;
				}

				Result fetch(const std::string& path, int64_t offset, const Sink& sink) override
				{
					struct Write { const Sink* sink; bool stopped = false; } w = { &sink };
					auto write = [](char* p, size_t sz, size_t n, void* u) -> size_t
					{
						Write* w = (Write*)u;
						if (!(*w->sink)(p, sz * n)) { w->stopped = true; return 0; }
						return sz * n;
					};
					prepare(url(cleanPath(path), false));
					curl_easy_setopt(h, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)offset);
					curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, (curl_write_callback)write);
					curl_easy_setopt(h, CURLOPT_WRITEDATA, &w);
					Result r = perform("Downloading " + path);
					if (w.stopped) return Result(Err::STOPPED, "Stopped.");
					return r;
				}

				std::string fingerprint() const override { return fp; }
				std::string home() const override { return homeDir; }
				void abort() override { aborted = true; }

			private:
				Server s;
				const uint16_t* table;
				std::string homeDir = "/";
				CURL* h = nullptr;
				curl_slist* quote = nullptr;
				char errbuf[CURL_ERROR_SIZE];
				long code = 0;					// the server's last reply code
				bool noMlsd = false, pinFailed = false, pinnable = true;
				// OPTS UTF8 ON: with this request; libcurl's id of the connection the last request went on, and of the
				// one that had it (curl_off_t: CURLINFO_CONN_ID's; a libcurl before 8.2 has none, and every request has it)
				bool quoted = false;
				curl_off_t lastConn = -1, utf8Conn = -2;
				std::atomic<bool> aborted{ false };
				std::string fp;

				// "ftp://host:port/%2F<path>": %2F makes the path absolute, each part escaped (in the server's code page)
				std::string url(const std::string& path, bool dir)
				{
					std::string host = s.host.find(':') != std::string::npos && s.host[0] != '[' ? "[" + s.host + "]" : s.host;
					std::string u = "ftp://" + host + ":" + std::to_string(s.portOrDefault()) + "/%2F";
					std::string p = charset::fromUtf8(cleanPath(path), table);
					size_t a = 1;
					while (a < p.size())
					{
						size_t b = p.find('/', a);
						if (b == std::string::npos) b = p.size();
						char* e = curl_easy_escape(h, p.c_str() + a, (int)(b - a));
						if (e) { u += e; curl_free(e); }
						if (b < p.size()) u += "/";
						a = b + 1;
					}
					if (dir && u.back() != '/') u += "/";
					return u;
				}

				static int progress(void* me, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
				{
					return ((FtpBackend*)me)->aborted ? 1 : 0;
				}

				static int exIndex()
				{
					static int idx = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
					return idx;
				}

				// the certificate: pinned on first use, its SHA-256 (of the DER) checked during the handshake
				static int verify(int, X509_STORE_CTX* store)
				{
					if (X509_STORE_CTX_get_error_depth(store) != 0) return 1;	// only the server's own certificate counts
					SSL* ssl = (SSL*)X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());
					FtpBackend* me = ssl ? (FtpBackend*)SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), exIndex()) : nullptr;
					X509* cert = X509_STORE_CTX_get_current_cert(store);
					if (!me || !cert) return 0;
					unsigned char md[EVP_MAX_MD_SIZE];
					unsigned n = 0;
					if (!X509_digest(cert, EVP_sha256(), md, &n)) return 0;
					me->fp = "SHA256:" + base64(md, n, false);
					if (!pinMatches(me->s.hostkey, me->fp)) { me->pinFailed = true; return 0; }
					return 1;
				}

				static CURLcode sslContext(CURL*, void* ctx, void* me)
				{
					SSL_CTX_set_ex_data((SSL_CTX*)ctx, exIndex(), me);
					SSL_CTX_set_verify((SSL_CTX*)ctx, SSL_VERIFY_PEER, verify);
					return CURLE_OK;
				}

				void prepare(const std::string& u)
				{
					// (asked before the reset forgets it: whether the last request's connection is there for this one)
					curl_socket_t live = CURL_SOCKET_BAD;
					curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &live);
					curl_easy_reset(h);	// (the connection stays: listings and downloads reuse the login)
					errbuf[0] = 0;
					code = 0;
					pinFailed = false;
					std::string user = s.guest || s.user.empty() ? "anonymous" : s.user;
					std::string pass = s.guest || s.user.empty() ? "banana@" : s.password;
					curl_easy_setopt(h, CURLOPT_URL, u.c_str());
					curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
					curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
					curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
					curl_easy_setopt(h, CURLOPT_SERVER_RESPONSE_TIMEOUT, 30L);
					// no byte for 30 s: the link is gone (Wi-Fi), the transfer ends and is tried again
					curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
					curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
					curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE, 1L);
					curl_easy_setopt(h, CURLOPT_FTP_FILEMETHOD, (long)CURLFTPMETHOD_NOCWD);
					curl_easy_setopt(h, CURLOPT_USERNAME, user.c_str());
					curl_easy_setopt(h, CURLOPT_PASSWORD, pass.c_str());
					curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
					curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, progress);
					curl_easy_setopt(h, CURLOPT_XFERINFODATA, this);
					curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discard);
					// UTF-8 names asked for once a connection, before its first request (not again before each): a new one,
					// or one libcurl made again by itself for the last request
					quoted = quote && (live == CURL_SOCKET_BAD || lastConn != utf8Conn);
					if (quoted) curl_easy_setopt(h, CURLOPT_QUOTE, quote);
					if (s.tls)
					{
						curl_easy_setopt(h, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
						// home servers' certificates are their own: pinned instead (verify)
						curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
						curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
						pinnable = curl_easy_setopt(h, CURLOPT_SSL_CTX_FUNCTION, sslContext) == CURLE_OK;
						curl_easy_setopt(h, CURLOPT_SSL_CTX_DATA, this);
					}
				}

				Result perform(const std::string& what)
				{
					CURLcode rc = curl_easy_perform(h);
					curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
#if LIBCURL_VERSION_NUM >= 0x080200
					curl_easy_getinfo(h, CURLINFO_CONN_ID, &lastConn);
					if (quoted) utf8Conn = lastConn;
#endif
					if (rc == CURLE_OK) return Result();
					std::string why = errbuf[0] ? errbuf : curl_easy_strerror(rc);
					if (pinFailed)
					{
						Result r(Err::HOSTKEY, "The server's certificate isn't the one it had before.");
						r.got = fp;
						return r;
					}
					if (aborted) return Result(Err::NETWORK, "The connection was dropped.");
					switch (rc)
					{
					case CURLE_LOGIN_DENIED:
						return Result(Err::AUTH, "The server refused the name or the password.");
					case CURLE_REMOTE_FILE_NOT_FOUND:
						return Result(Err::NOT_FOUND, what + ": it isn't there.");
					case CURLE_REMOTE_ACCESS_DENIED:
						return Result(code == 550 ? Err::NOT_FOUND : Err::DENIED, what + ": " + why);
					case CURLE_FTP_COULDNT_USE_REST: case CURLE_BAD_DOWNLOAD_RESUME: case CURLE_RANGE_ERROR:
						return Result(Err::NO_RESUME, "The server can't resume a download.");
					case CURLE_OPERATION_TIMEDOUT:
						return Result(Err::TIMEOUT, what + ": no answer in time.");
					case CURLE_COULDNT_RESOLVE_HOST:
						return Result(Err::NETWORK, "No server named " + s.host + " was found.");
					case CURLE_COULDNT_CONNECT:
						return Result(Err::NETWORK, s.host + " can't be reached, or refused the connection: is the protocol and the port right?");
					case CURLE_USE_SSL_FAILED:
						return Result(Err::UNSUPPORTED, s.host + " doesn't do FTPS (explicit TLS): turn FTPS off for it, or use SFTP.");
					case CURLE_ABORTED_BY_CALLBACK: case CURLE_WRITE_ERROR:
						return Result(Err::STOPPED, "Stopped.");
					case CURLE_RECV_ERROR: case CURLE_SEND_ERROR: case CURLE_PARTIAL_FILE: case CURLE_GOT_NOTHING:
					case CURLE_SSL_CONNECT_ERROR: case CURLE_FTP_ACCEPT_FAILED: case CURLE_FTP_ACCEPT_TIMEOUT:
					case CURLE_FTP_CANT_GET_HOST: case CURLE_FTP_WEIRD_PASV_REPLY: case CURLE_FTP_WEIRD_227_FORMAT:
					case CURLE_FTP_WEIRD_SERVER_REPLY:
						return Result(Err::NETWORK, what + ": " + why);
					default:
						break;
					}
					if (code == 530) return Result(Err::AUTH, "The server refused the name or the password.");
					if (code == 550) return Result(Err::NOT_FOUND, what + ": it isn't there, or not allowed.");
					return Result(Err::OTHER, what + ": " + why);
				}
			};
		}

		std::unique_ptr<Backend> makeFtp(const Server& s) { return std::unique_ptr<Backend>(new FtpBackend(s)); }
	}
}
