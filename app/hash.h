#ifndef hash_h
#define hash_h

#include "shared.h"
#include "type.h"
#include <boost/filesystem.hpp>
#include <array>

namespace bfs = boost::filesystem;

typedef std::array<uint8_t, 16> HashType;

std::string FormatHash(HashType const &Hash);

Optional<HashType> UnformatHash(char const *String);

Optional<std::pair<HashType, size_t>> HashFile(bfs::path const &Path);

#endif
