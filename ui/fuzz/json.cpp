// Fuzz target: a theme's JSON (json.cpp), as read from the card. The value walked after a parse, as the theme code does.
#include "../src/json.h"
#include <cstddef>
#include <cstdint>

using namespace banana;

static size_t walk(const JVal* v, int depth)
{
	if (!v || depth > 64) return 0;
	size_t n = v->s.size();
	for (const auto& e : v->arr) n += walk(e.get(), depth + 1);
	for (const auto& m : v->obj) n += m.first.size() + walk(m.second.get(), depth + 1);
	n += v->str("name").size() + (v->num("w") > 0) + v->flag("debug");
	return n;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	std::string why;
	JPtr doc = parseJson(std::string((const char*)data, size), &why);
	if (doc) walk(doc.get(), 0);
	return 0;
}
