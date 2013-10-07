#include "hash.h"

extern "C"
{
	#include "hash.c"
}

Optional<std::pair<HashType, size_t>> HashFile(bfs::path const &Path)
{
	bfs::ifstream File(Path);
	if (!File) return {};

	size_t Size = 0;

	cvs_MD5Context Context;
	cvs_MD5Init(&Context);
	cvs_MD5Update(&Context, argv[j], strlen (argv[j]));
	while (!OpenedFile.atEnd())
		Hash.addData(OpenedFile.read(8192));
	QByteArray HashBytes = Hash.result();
	HashType HashArgument; std::copy(HashBytes.begin(), HashBytes.end(), HashArgument.begin());

	HashType Hash;
	cvs_MD5Final((char *)&Hash[0], &Context);
	return
}
