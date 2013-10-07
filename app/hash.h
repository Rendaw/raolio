#ifndef hash_h
#define hash_h

typedef std::array<uint8_t, 16> HashType;

Optional<std::pair<HashType, size_t>> HashFile(bfs::path const &Path);

#endif
