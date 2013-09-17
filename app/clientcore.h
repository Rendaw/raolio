#ifndef clientcore_h
#define clientcore_h

typedef std::array<uint8_t, 16> HashType;

struct MediaItem
{
	HashType Hash;
	std::string Filename;
};

struct Core
{
	Core(bool Listen, std::string const &Host, uint16_t Port);
};

struct ClientCore
{
	ClientCore(bool Listen, std::string const &Host, uint16_t Port);
	~ClientCore(void);
	
	void Add(HashType const &Hash, std::string const &Filename);
	
	libvlc_instance_t *VLC;
	libvlc_media_player_t *VLCMediaPlayer;
	bfs::path TempPath;
	std::list<std::tuple<HashType, std::string>> Media;
	std::map<HashType, std::string> MediaLookup;
	
	Core Event;
};

#endif
