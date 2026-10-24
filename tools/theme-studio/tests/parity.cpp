// Banana Theme Studio: the expression parity harness.
//
// Evaluates tests/expr-cases.txt through ui/src/expr.cpp — the engine that actually draws a theme — and
// prints one line per case: "<expr>\ts|<string>" or "<expr>\tn|<number>". tests/expr.test.js runs the same
// cases through tools/theme-studio/expr.js and compares, so any drift between the port and the engine shows
// up as a failing test. Build with:
//   g++ -O2 -std=c++17 -I ui/src tools/theme-studio/tests/parity.cpp ui/src/expr.cpp -o /tmp/banana-parity
#include "expr.h"
#include <cstdio>
#include <fstream>
#include <string>

using namespace banana;

static Scope makeScope()
{
	Scope sc;
	sc.set("x", EV(3.0));
	sc.set("y", EV(0.5));
	sc.set("zero", EV(0.0));
	sc.set("one", EV(1.0));
	sc.set("n", EV(7.0));
	sc.set("name", EV(std::string("Banana")));
	sc.set("msg", EV(std::string("hello")));
	sc.set("empty", EV(std::string("")));
	sc.set("col", EV(std::string("#ff8000")));
	return sc;
}

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: parity CASES.txt\n"); return 2; }
	std::ifstream in(argv[1]);
	if (!in) { fprintf(stderr, "can't read %s\n", argv[1]); return 2; }
	std::string line;
	while (std::getline(in, line))
	{
		// trim trailing whitespace and \r
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
		if (line.empty() || line[0] == '#') continue;
		Scope sc = makeScope();
		std::string why;
		Expr e = compileExpr(line, &why);
		if (!e) { printf("%s\tERR\n", line.c_str()); continue; }
		EV v = evalExpr(e, sc);
		if (v.isStr) printf("%s\ts|%s\n", line.c_str(), v.s.c_str());
		else printf("%s\tn|%s\n", line.c_str(), v.str().c_str());
	}
	return 0;
}