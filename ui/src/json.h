#pragma once
// banana-ui: a JSON document reader for themes. Objects keep their members' order (a layout's children are drawn in
// it), // and /* */ comments and trailing commas are allowed (a theme is written by hand).
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace banana
{
	struct JVal;
	typedef std::shared_ptr<const JVal> JPtr;

	struct JVal
	{
		enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
		bool b = false;
		double n = 0;
		std::string s;
		std::vector<JPtr> arr;
		std::vector<std::pair<std::string, JPtr>> obj;	// in the file's order

		bool isObj() const { return kind == OBJ; }
		bool isArr() const { return kind == ARR; }
		bool isStr() const { return kind == STR; }
		bool isNum() const { return kind == NUM; }
		// a member (null when there is none, or this isn't an object)
		const JVal* get(const std::string& key) const;
		std::string str(const std::string& key, const std::string& def = "") const;
		double num(const std::string& key, double def = 0) const;
		bool flag(const std::string& key, bool def = false) const;
	};

	// the document, or null with the reason (and where) in *why
	JPtr parseJson(const std::string& text, std::string* why = nullptr);
	JPtr readJsonFile(const std::string& path, std::string* why = nullptr);
}
