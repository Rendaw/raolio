#ifndef clientcore_h
#define clientcore_h

#include "shared.h"
#include "core.h"

#include <vlc/vlc.h>
#include <map>
#include <vector>
#include <boost/filesystem.hpp>

namespace bfs = boost::filesystem;

struct LatencyTracker
{
	// Cheap implementation
	void Add(uint64_t Instance, uint64_t Sent)
	{
		auto Milliseconds = Sent - GetNow();
		if (Milliseconds > Max) Max = Milliseconds;
	}

	uint64_t Expected(void) const
	{
		return Max * 2;
	}

	private:
		uint64_t Max = 0;
};

struct EngineWrapper
{
	EngineWrapper(void);
	~EngineWrapper(void);

	libvlc_instance_t *VLC;
	libvlc_media_player_t *VLCMediaPlayer;
};

struct MediaInfo
{
	HashType Hash;
	bfs::path Filename;
	Optional<uint16_t> Track;
	std::string Artist;
	std::string Album;
	std::string Title;
};

struct MediaItem : MediaInfo
{
	libvlc_media_t *VLCMedia;

	MediaItem(HashType const &Hash, bfs::path const &Filename, Optional<uint16_t> const &Track, std::string const &Artist, std::string const &Album, std::string const &Title, libvlc_media_t *VLCMedia);
	~MediaItem(void);
};

struct ClientCore
{
	ClientCore(ClientCore const &Other) = delete;
	ClientCore(ClientCore &&Other) = delete;
	ClientCore(std::string const &Handle, float Volume);

	std::function<void(std::string const &Message)> LogCallback;
	std::function<void(float Time)> SeekCallback;
	std::function<void(MediaInfo Item)> AddCallback;
	std::function<void(MediaInfo Item)> UpdateCallback;
	std::function<void(HashType const &MediaID)> SelectCallback;
	std::function<void(void)> PlayCallback;
	std::function<void(void)> StopCallback;
	std::function<void(void)> EndCallback;

	void Open(bool Listen, std::string const &Host, uint16_t Port);

	void Add(HashType const &Hash, size_t Size, bfs::path const &Filename);

	void SetVolume(float Volume);
	void GetTime(void);

	void Play(HashType const &MediaID, uint64_t Position);
	void Play(HashType const &MediaID, float Position);
	void Play(void);
	void Stop(void);
	void Chat(std::string const &Message);

	private:
		void AddInternal(HashType const &Hash, bfs::path const &Filename);

		void SetVolumeInternal(float Volume);
		void SeekInternal(float Time);
		float GetTimeInternal(void);

		void LocalPlayInternal(HashType const &MediaID, uint64_t Position);
		void PlayInternal(HashType const &MediaID, uint64_t Position, uint64_t SystemTime, uint64_t Now);
		void PlayInternal(HashType const &MediaID, float Position);
		void PlayInternal(void);
		void LocalStopInternal(void);
		void StopInternal(void);
		bool IsPlayingInternal(void);

		static void VLCMediaEndCallback(libvlc_event_t const *Event, void *UserData);
		static void VLCMediaParsedCallback(libvlc_event_t const *Event, void *UserData);

		CallTransferType &CallTransfer; // Makes a call in the core's main thread

		Core Parent;

		EngineWrapper Engine;
		std::map<HashType, std::unique_ptr<MediaItem>> MediaLookup;
		MediaItem *Playing;

		LatencyTracker Latencies;

		std::string const Handle;
};

#endif
