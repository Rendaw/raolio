#include "hash.h"

#include <boost/filesystem/fstream.hpp>
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

OptionalT<std::pair<HashT, size_t>> HashFile(bfs::path const &Path)
{
	bfs::ifstream File(Path);
	if (!File) return {};

	size_t Size = 0;

	cvs_MD5Context Context;
	cvs_MD5Init(&Context);

	std::vector<uint8_t> Buffer(8192);
	while (File)
	{
		File.read((char *)&Buffer[0], static_cast<long>(Buffer.size()));
		std::streamsize Read = File.gcount();
		if (Read <= 0) break;
		Size += static_cast<size_t>(Read);
		cvs_MD5Update(&Context, &Buffer[0], static_cast<unsigned int>(Read));
	}
	HashT Hash{};
	cvs_MD5Final(&Hash[0], &Context);
	return std::make_pair(Hash, Size);
}
