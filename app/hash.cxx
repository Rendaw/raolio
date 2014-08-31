#include "hash.h"

#include <iomanip>

extern "C"
{
	#include "md5.h"
}

std::string FormatHash(HashT const &Hash)
{
	std::stringstream Display;
	Display << std::hex << std::setw(2);
	for (auto Byte : Hash) Display << static_cast<unsigned int>(Byte);
	return Display.str();
}

OptionalT<HashT> UnformatHash(char const *String)
{
	HashT Hash;
	for (size_t Position = 0; Position < Hash.size() * 2; Position += 2)
	{
		char const First = String[Position];
		if ((First >= 'a') && (First <= 'z')) Hash[Position / 2] = static_cast<uint8_t>((First - 'a' + 10) << 4);
		else if ((First >= '0') && (First <= '9')) Hash[Position / 2] = static_cast<uint8_t>((First - '0') << 4);
		else return {};

		char const Second = String[Position + 1];
		if ((Second >= 'a') && (Second <= 'z')) Hash[Position / 2] |= static_cast<uint8_t>(Second - 'a' + 10);
		else if ((Second >= '0') && (Second <= '9')) Hash[Position / 2] |= static_cast<uint8_t>(Second - '0');
		else return {};
	}
	return Hash;
}

OptionalT<std::pair<HashT, size_t>> HashFile(PathT const &Path)
{
	FILE *File(fopen(Path->Render().c_str(), "r"));
	if (!File) return {};

	size_t Size = 0;

	cvs_MD5Context Context;
	cvs_MD5Init(&Context);

	std::vector<uint8_t> Buffer(8192);
	while (File)
	{
		size_t Read = fread((char *)&Buffer[0], 1, Buffer.size(), File);
		if (Read <= 0) break;
		Size += Read;
		cvs_MD5Update(&Context, &Buffer[0], static_cast<unsigned int>(Read));
	}
	fclose(File);
	HashT Hash{};
	cvs_MD5Final(&Hash[0], &Context);
	return std::make_pair(Hash, Size);
}
