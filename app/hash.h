#ifndef hash_h
#define hash_h

#include "shared.h"
#include "type.h"
#include <boost/filesystem.hpp>
#include <array>

namespace bfs = boost::filesystem;

typedef std::array<uint8_t, 16> HashT;

std::string FormatHash(HashT const &Hash);

Optional<HashT> UnformatHash(char const *String);

Optional<std::pair<HashT, size_t>> HashFile(bfs::path const &Path);

#endif
