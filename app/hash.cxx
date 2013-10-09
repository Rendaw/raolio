#include "hash.h"

#include <boost/filesystem/fstream.hpp>
#include <iomanip>

extern "C"
{
	#include "md5.h"
}

std::string FormatHash(HashType const &Hash)
{
	std::stringstream Display;
	Display << std::hex << std::setw(2);
	for (auto Byte : Hash) Display << static_cast<unsigned int>(Byte);
	return Display.str();
}

Optional<HashType> UnformatHash(char const *String)
{
	HashType Hash;
	for (size_t Position = 0; Position < Hash.size(); Position += 2)
	{
		if ((String[Position] < 'a') || 
			(String[Position] > 'z') || 
			(String[Position + 1] < 'a') ||
			(String[Position + 1] > 'z'))
			return {};
		
		Hash[Hash.size() - 1 - Position] = 
			(String[Position] - 'a') << 4 |
			(String[Position + 1] - 'a');
	}
	return Hash;
}

Optional<std::pair<HashType, size_t>> HashFile(bfs::path const &Path)
{
	bfs::ifstream File(Path);
	if (!File) return {};

	size_t Size = 0;

	cvs_MD5Context Context;
	cvs_MD5Init(&Context);
	
	std::vector<uint8_t> Buffer(8192);
	while (File)
	{
		File.read((char *)&Buffer[0], Buffer.size());
		size_t Read = File.gcount();
		if (Read == 0) break;
		Size += Read;
		cvs_MD5Update(&Context, &Buffer[0], Read);
	}
	HashType Hash;
	cvs_MD5Final(&Hash[0], &Context);
	return std::make_pair(Hash, Size);
}
