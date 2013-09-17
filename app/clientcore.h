#ifndef clientcore_h
#define clientcore_h

#include <vlc/vlc.h>
#include <array>
#include <map>
#include <vector>
#include <boost/filesystem.hpp>

namespace bfs = boost::filesystem;

typedef std::array<uint8_t, 16> HashType;

struct MediaItem
{
	inline bool operator <(MediaItem const &Other) const { return Filename < Other.Filename; }
	HashType Hash;
	std::string Filename;
};

struct ClientCore
{
	ClientCore(void);
	//ClientCore(bool Listen, std::string const &Host, uint16_t Port);
	~ClientCore(void);

	std::function<void(std::string const &Message)> LogCallback;

	std::vector<MediaItem> const &GetPlaylist(void) const;

	void Command(std::string const &CommandString);

	void Add(HashType const &Hash, std::string const &Filename);
	void Shuffle(void);
	void Sort(void);

	void SetVolume(float Volume);
	void Seek(float Time);
	float GetTime(void);

	void PlayStop(void);
	bool IsPlaying(void);
	void Next(void);
	void Previous(void);
	void Select(size_t Which);

	private:
		libvlc_instance_t *VLC;
		libvlc_media_player_t *VLCMediaPlayer;
		bfs::path TempPath;
		size_t PlaylistIndex;
		std::vector<MediaItem> Media;
		std::map<HashType, std::string> MediaLookup;
};

#endif
