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

struct EngineWrapper
{
	EngineWrapper(void);
	~EngineWrapper(void);

	libvlc_instance_t *VLC;
	libvlc_media_player_t *VLCMediaPlayer;
};

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
	ClientCore(std::string const &Host, uint16_t Port);

	std::function<void(std::string const &Message)> LogCallback;
	std::function<void(void)> StoppedCallback;
	std::function<void(void)> MediaUpdatedCallback;
	std::function<void(size_t Which)> MediaRemovedCallback;
	std::function<void(void)> MediaAddedCallback;

	void Add(HashType const &Hash, bfs::path const &Filename);

	void SetVolume(float Volume);
	float GetTime(void);

	void Play(HashType const &MediaID, uint64_t Position);
	void Play(HashType const &MediaID, float Position);
	void Play(void);
	void Stop(void);

	private:
		void AddInternal(HashType const &Hash, bfs::path const &Filename);

		void SetVolumeInternal(float Volume);
		void SeekInternal(float Time);
		float GetTimeInternal(void);

		void LocalPlayInternal(HashType const &MediaID, uint64_t Position);
		void PlayInternal(HashType const &MediaID, uint64_t Position, uint64_t SystemTime, uint64_t Now);
		void PlayInternal(HashType const &MediaID, float Position);
		void PlayInternal(void);
		void StopInternal(void);
		bool IsPlayingInternal(void);

		static void VLCMediaEndCallback(libvlc_event_t const *Event, void *UserData);
		static void VLCMediaParsedCallback(libvlc_event_t const *Event, void *UserData);

		CallTransferType &CallTransfer; // Makes a call in the core's main thread

		Core Parent;

		EngineWrapper Engine;
		std::vector<std::unique_ptr<MediaItem>> Media;
		std::map<HashType, MediaItem *> MediaLookup;

		std::vector<std::unique_ptr<ExtraScopeItem>> ExtraScope;
};

#endif
