#include "zip.h"
#include "zipdir.h"

namespace banana
{
	bool readZip(const std::string& zip, const std::function<int(const std::vector<rnds::ZipMember>&)>& pick,
		const std::function<bool(const uint8_t*, size_t)>& sink)
	{
		ZipDir z;
		if (!z.open(zip)) return false;
		std::vector<rnds::ZipMember> members;
		for (const ZipEntry& e : z.entries())
		{
			rnds::ZipMember m;
			m.name = e.name;
			m.size = e.size;
			members.push_back(m);
		}
		int k = pick(members);
		if (k < 0 || k >= (int)members.size()) return false;
		return z.read(z.entries()[k], sink);
	}
}
