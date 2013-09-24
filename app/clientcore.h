#ifndef clientcore_h
#define clientcore_h

#include "shared.h"

#include <vlc/vlc.h>
#include <array>
#include <map>
#include <vector>
#include <boost/filesystem.hpp>

namespace bfs = boost::filesystem;

typedef std::array<uint8_t, 16> HashType;

struct MediaItem
{
	bool Playing;
	HashType Hash;
	bfs::path Filename;
	std::string Artist;
	std::string Album;
	std::string Title;

	libvlc_media_t *VLCMedia;

	~MediaItem(void);
};

struct ExtraScopeItem { virtual ~ExtraScopeItem(void); };
struct ClientCore
{
	ClientCore(ClientCore const &Other) = delete;
	ClientCore(ClientCore &&Other) = delete;
	ClientCore(CallTransferType &CallTransfer);
	//ClientCore(bool Listen, std::string const &Host, uint16_t Port);
	~ClientCore(void);

	std::function<void(std::string const &Message)> LogCallback;
	std::function<void(void)> StoppedCallback;
	std::function<void(void)> MediaUpdatedCallback;
	std::function<void(size_t Which)> MediaRemovedCallback;
	std::function<void(void)> MediaAddedCallback;

	std::vector<std::unique_ptr<MediaItem>> const &GetPlaylist(void) const;

	void Command(std::string const &CommandString);

	void Add(HashType const &Hash, bfs::path const &Filename);
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
		static void VLCMediaEndCallback(libvlc_event_t const *Event, void *UserData);
		static void VLCMediaParsedCallback(libvlc_event_t const *Event, void *UserData);

		CallTransferType &CallTransfer; // Makes a call in the core's main thread

		libvlc_instance_t *VLC;
		libvlc_media_player_t *VLCMediaPlayer;
		size_t PlaylistIndex;
		std::vector<std::unique_ptr<MediaItem>> Media;
		std::map<HashType, MediaItem *> MediaLookup;

		std::vector<std::unique_ptr<ExtraScopeItem>> ExtraScope;
};

#endif
