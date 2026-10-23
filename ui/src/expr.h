#pragma once
// banana-ui: the expression language of layout themes (layout.h). A property written "=..." is an expression, a text
// with {...} in it has expressions inside the braces:
//
//   "x": "=pos * 120 + 320",  "text": "{game.plays} plays",  "fill": "=sel ? accent : '#202020'"
//
// Values are numbers or strings (true is 1, false 0; a string is true when it isn't empty or "0"). Operators: + - * /
// % (+ joins when either side is a string), == != < <= > >=, && || !, a ? b : c, and functions (expr.cpp's list:
// min, max, clamp, abs, floor, round, sin, ease functions, mix and alpha for colours, upper, len, ...). Names are
// looked up in the scope: a list item's own (item.*, i, sel, ...), the theme's vars, then the menu's hooks.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace banana
{
	struct EV
	{
		bool isStr = false;
		double n = 0;
		std::string s;
		EV() {}
		EV(double v) : n(v) {}
		EV(const std::string& v) : isStr(true), s(v) {}
		EV(const char* v) : isStr(true), s(v) {}
		bool truthy() const { return isStr ? !s.empty() && s != "0" && s != "false" : n != 0; }
		double num() const;
		std::string str() const;
	};

	// where names come from: a chain of scopes, the innermost first
	struct Scope
	{
		const Scope* parent = nullptr;
		std::vector<std::pair<std::string, EV>> vars;
		// names the scope works out when asked (the menu's hooks): true when it knows the name
		std::function<bool(const std::string&, EV&)> resolve;
		void set(const std::string& k, const EV& v) { for (auto& e : vars) if (e.first == k) { e.second = v; return; } vars.emplace_back(k, v); }
		bool lookup(const std::string& name, EV& out) const;
	};

	struct ExprNode;
	typedef std::shared_ptr<const ExprNode> Expr;
	// null (and the reason in *why) when the text isn't an expression
	Expr compileExpr(const std::string& text, std::string* why = nullptr);
	EV evalExpr(const Expr& e, const Scope& sc);

	// a property's text: "=expr", a template with {expr} parts ("{{" for a brace), or plain text
	struct Prop
	{
		enum Kind { LITERAL, EXPR, TEMPLATE } kind = LITERAL;
		EV lit;
		Expr expr;
		std::vector<std::pair<std::string, Expr>> parts;	// template: text before each expression (the last: none)
		std::string error;
		static Prop parse(const std::string& text);
		static Prop number(double v) { Prop p; p.lit = EV(v); return p; }
		EV eval(const Scope& sc) const;
	};

	// "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", "rgb(r,g,b)", "rgba(r,g,b,a)", "transparent", or a few names -> 0xAARRGGBB
	bool parseColorText(const std::string& v, uint32_t& out);
	std::string colorText(uint32_t argb);	// "#rrggbbaa"
}
