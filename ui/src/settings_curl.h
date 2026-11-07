#pragma once
// banana-ui: libcurl, dlopen()ed when a page needs it (busybox's wget has no HTTPS): the RetroAchievements account
// check (settings.cpp) and the game panel's achievements (settings_gamepanel.cpp); a field of the server's JSON
#include <dlfcn.h>
#include <string>

namespace banana
{
	struct Curl
	{
		void* (*init)(void) = nullptr;
		int (*setopt)(void*, int, ...) = nullptr;
		int (*perform)(void*) = nullptr;
		int (*getinfo)(void*, int, ...) = nullptr;
		void (*cleanup)(void*) = nullptr;
		char* (*escape)(void*, const char*, int) = nullptr;
		void (*freep)(void*) = nullptr;
		bool load()
		{
			void* l = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
			if (!l) return false;
			init = (void* (*)(void))dlsym(l, "curl_easy_init");
			setopt = (int (*)(void*, int, ...))dlsym(l, "curl_easy_setopt");
			perform = (int (*)(void*))dlsym(l, "curl_easy_perform");
			getinfo = (int (*)(void*, int, ...))dlsym(l, "curl_easy_getinfo");
			cleanup = (void (*)(void*))dlsym(l, "curl_easy_cleanup");
			escape = (char* (*)(void*, const char*, int))dlsym(l, "curl_easy_escape");
			freep = (void (*)(void*))dlsym(l, "curl_free");
			return init && setopt && perform && getinfo && cleanup && escape && freep;
		}
	};
	inline size_t onBody(char* p, size_t sz, size_t n, void* u) { ((std::string*)u)->append(p, sz * n); return sz * n; }
	// "Key":"value" or "Key":123 in the server's JSON (enough for its flat login reply)
	inline std::string jsonField(const std::string& j, const std::string& key)
	{
		size_t at = j.find("\"" + key + "\"");
		if (at == std::string::npos) return "";
		at = j.find(':', at);
		if (at == std::string::npos) return "";
		at++;
		while (at < j.size() && j[at] == ' ') at++;
		if (at < j.size() && j[at] == '"')
		{
			std::string v;
			for (size_t i = at + 1; i < j.size() && j[i] != '"'; i++) v += j[i] == '\\' && i + 1 < j.size() ? j[++i] : j[i];
			return v;
		}
		size_t e = at;
		while (e < j.size() && j[e] != ',' && j[e] != '}') e++;
		return j.substr(at, e - at);
	}}
