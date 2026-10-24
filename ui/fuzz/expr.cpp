// Fuzz target: a layout theme's property text (expr.cpp): "=expressions", {templates} and colours, compiled and
// evaluated against a scope that knows every name (a number for some, a string for the rest), as the menu's hooks do.
#include "../src/expr.h"
#include <cstddef>
#include <cstdint>

using namespace banana;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	std::string text((const char*)data, size);
	Scope sc;
	sc.set("sel", EV(1.0));
	sc.set("pos", EV(-2.5));
	sc.set("accent", EV("#f0b429"));
	sc.resolve = [](const std::string& name, EV& out)
	{
		if (name.size() % 2) out = EV((double)name.size());
		else out = EV(name);
		return true;
	};
	Prop p = Prop::parse(text);
	EV v = p.eval(sc);
	v.str();
	v.num();
	std::string why;
	if (Expr e = compileExpr(text, &why)) evalExpr(e, sc).truthy();
	uint32_t c;
	if (parseColorText(text, c)) colorText(c);
	return 0;
}
