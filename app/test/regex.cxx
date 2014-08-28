#include "../regex.h"

int main(int, char **)
{
	{
		std::string a, c;
		assert((Regex::ParserT<std::string, Regex::Ignore, std::string>{"(one)(two)(three)"}("onetwothree", a, c)));
		assert(a == "one");
		assert(c == "three");
	}

	{
		struct QEnumT : Regex::EnumerationT<int> { QEnumT(void) : EnumerationT{{"alpha", 0}, {"q", 1}} {} };
		std::string a, c;
		int b;
		assert((Regex::ParserT<std::string, QEnumT, std::string>{"(one)(alpha|q)(three)"}("oneqthree", a, b, c)));
		assert(a == "one");
		assert(b == 1);
		assert(c == "three");
	}
	return 0;
}
