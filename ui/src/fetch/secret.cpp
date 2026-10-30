// banana-fetch: the passwords kept in nas.conf, sealed (servers.h says how); OpenSSL's, so banana-ui, which only reads
// the file's other lines, doesn't link it
#include "servers.h"
#include "names.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace banana
{
	namespace fetch
	{
		std::string newId()
		{
			unsigned char b[8];
			if (RAND_bytes(b, sizeof b) != 1) for (auto& x : b) x = (unsigned char)rand();
			char h[17];
			for (int i = 0; i < 8; i++) snprintf(h + i * 2, 3, "%02x", b[i]);
			return h;
		}

		// ---- the sealed passwords ---------------------------------------------------------------------------------
		static const char* const PREFIX = "v2:";

		// where a password goes: it only opens for the same protocol, host, port and user
		static std::string context(const ServerRec& s)
		{
			return s.protocol + std::string(1, '\0') + lower(s.host) + std::string(1, '\0') + std::to_string(s.portOrDefault()) +
				std::string(1, '\0') + s.user;
		}

		bool SecretBox::open(const std::string& keyPath, const std::string& serial)
		{
			unsigned char file[32];
			bool have = false;
			{
				std::ifstream f(keyPath, std::ios::binary);
				std::string k((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
				if (k.size() == 32) { memcpy(file, k.data(), 32); have = true; }
			}
			if (!have)
			{
				// the first password kept (or a damaged key: the passwords sealed with it are asked for again)
				if (RAND_bytes(file, sizeof file) != 1) return false;
				std::string tmp = keyPath + ".new";
				int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
				if (fd < 0) return false;
				bool ok = write(fd, file, sizeof file) == (ssize_t)sizeof file && fsync(fd) == 0;
				close(fd);
				if (!ok || rename(tmp.c_str(), keyPath.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			}
			// the SoC's serial number (the device tree's /serial-number, which U-Boot fills in): none on a computer
			std::string id, from = serial;
			const char* t = getenv("BANANA_FETCH_TEST");
			const char* ts = getenv("BANANA_FETCH_TEST_SERIAL");
			if (t && !strcmp(t, "1") && ts && *ts) from = ts;	// (the tests: another handheld)
			{
				std::ifstream f(from, std::ios::binary);
				std::string v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
				for (char c : v) if (c != '\0' && c != ' ' && c != '\n' && c != '\r' && c != '\t') id += c;
				if (id.find_first_not_of('0') == std::string::npos) id.clear();
			}
			EVP_PKEY_CTX* pc = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
			size_t len = sizeof key;
			const char* info = "banana nas.conf passwords v2";
			bool ok = pc && EVP_PKEY_derive_init(pc) > 0 && EVP_PKEY_CTX_set_hkdf_md(pc, EVP_sha256()) > 0 &&
				(id.empty() || EVP_PKEY_CTX_set1_hkdf_salt(pc, (const unsigned char*)id.data(), (int)id.size()) > 0) &&
				EVP_PKEY_CTX_set1_hkdf_key(pc, file, sizeof file) > 0 &&
				EVP_PKEY_CTX_add1_hkdf_info(pc, (const unsigned char*)info, (int)strlen(info)) > 0 &&
				EVP_PKEY_derive(pc, key, &len) > 0 && len == sizeof key;
			EVP_PKEY_CTX_free(pc);
			memset(file, 0, sizeof file);
			ready = ok;
			withSerial = !id.empty();
			return ok;
		}

		std::string SecretBox::seal(const std::string& plain, const ServerRec& s)
		{
			if (!ready) return "";
			unsigned char nonce[12], tag[16];
			if (RAND_bytes(nonce, sizeof nonce) != 1) return "";
			std::string aad = context(s);
			std::vector<unsigned char> out(sizeof nonce + plain.size() + sizeof tag);
			memcpy(out.data(), nonce, sizeof nonce);
			EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
			int n = 0, m = 0;
			bool ok = c && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) > 0 &&
				EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, sizeof nonce, nullptr) > 0 &&
				EVP_EncryptInit_ex(c, nullptr, nullptr, key, nonce) > 0 &&
				EVP_EncryptUpdate(c, nullptr, &n, (const unsigned char*)aad.data(), (int)aad.size()) > 0 &&
				EVP_EncryptUpdate(c, out.data() + sizeof nonce, &n, (const unsigned char*)plain.data(), (int)plain.size()) > 0 &&
				EVP_EncryptFinal_ex(c, out.data() + sizeof nonce + n, &m) > 0 &&
				EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, sizeof tag, tag) > 0;
			EVP_CIPHER_CTX_free(c);
			if (!ok) return "";
			memcpy(out.data() + sizeof nonce + plain.size(), tag, sizeof tag);
			return PREFIX + base64(out.data(), out.size());
		}

		bool SecretBox::unseal(const std::string& sealed, const ServerRec& s, std::string& plain)
		{
			if (!ready || sealed.compare(0, 3, PREFIX) != 0) return false;
			std::string b = sealed.substr(3);
			if (b.empty() || b.size() % 4) return false;
			std::vector<unsigned char> raw(b.size() / 4 * 3 + 1);
			int n = EVP_DecodeBlock(raw.data(), (const unsigned char*)b.data(), (int)b.size());
			if (n < 0) return false;
			n -= (b.end()[-1] == '=') + (b.end()[-2] == '=');	// (DecodeBlock counts the padding as bytes)
			if (n < 12 + 16) return false;
			const unsigned char* nonce = raw.data();
			const unsigned char* ct = raw.data() + 12;
			int ctLen = n - 12 - 16;
			unsigned char tag[16];
			memcpy(tag, raw.data() + n - 16, 16);
			std::string aad = context(s);
			std::vector<unsigned char> out((size_t)ctLen + 16);
			EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
			int k = 0, m = 0;
			bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) > 0 &&
				EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) > 0 &&
				EVP_DecryptInit_ex(c, nullptr, nullptr, key, nonce) > 0 &&
				EVP_DecryptUpdate(c, nullptr, &k, (const unsigned char*)aad.data(), (int)aad.size()) > 0 &&
				EVP_DecryptUpdate(c, out.data(), &k, ct, ctLen) > 0 &&
				EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag) > 0 &&
				EVP_DecryptFinal_ex(c, out.data() + k, &m) > 0;
			EVP_CIPHER_CTX_free(c);
			if (!ok) return false;
			plain.assign((const char*)out.data(), (size_t)(k + m));
			return true;
		}
	}
}
