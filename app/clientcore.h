#ifndef clientcore_h
#define clientcore_h

#include "shared.h"
#include "core.h"

#include <vlc/vlc.h>
#include <map>
#include <vector>
#include <boost/filesystem.hpp>

namespace bfs = boost::filesystem;

typedef StrictType(float) MediaTimePercentT;

struct LatencyTracker
{
	// Cheap implementation
	void Add(uint64_t Instance, uint64_t Sent)
	{
		auto Now = GetNow();
		if (Sent > Now) return; // Clocks out of synch probably
		auto Milliseconds = Now - Sent;
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
	HashT Hash;
	bfs::path Filename;
	OptionalT<uint16_t> Track;
	std::string Artist;
	std::string Album;
	std::string Title;
};

struct MediaItem : MediaInfo
{
	libvlc_media_t *VLCMedia;

	MediaItem(HashT const &Hash, bfs::path const &Filename, OptionalT<uint16_t> const &Track, std::string const &Artist, std::string const &Album, std::string const &Title, libvlc_media_t *VLCMedia);
	~MediaItem(void);
};

struct ClientCore
{
	ClientCore(ClientCore const &Other) = delete;
	ClientCore(ClientCore &&Other) = delete;
	ClientCore(float Volume);

	std::function<void(std::string const &Message)> LogCallback;
	std::function<void(float Percent, float Duration)> SeekCallback;
	std::function<void(MediaInfo Item)> AddCallback;
	std::function<void(HashT const &MediaID)> RemoveCallback;
	std::function<void(MediaInfo Item)> UpdateCallback;
	std::function<void(HashT const &MediaID)> SelectCallback;
	std::function<void(void)> PlayCallback;
	std::function<void(void)> StopCallback;
	std::function<void(void)> EndCallback;

	void Open(bool Listen, std::string const &Host, uint16_t Port);

	void Add(HashT const &Hash, size_t Size, bfs::path const &Filename);
	void Remove(HashT const &Hash);
	void RemoveAll(void);

	void SetVolume(float Volume);
	void GetTime(void);

	void Play(HashT const &MediaID, MediaTimeT Position);
	void Play(HashT const &MediaID, float Position);
	void Play(void);
	void Stop(void);
	void Chat(std::string const &Message);

	private:
		void AddInternal(HashT const &Hash, bfs::path const &Filename, std::string const &DefaultTitle);
		void RemoveInternal(HashT const &Hash);

		void SetVolumeInternal(float Volume);
		float GetTimeInternal(void);

		void LocalPlayInternal(HashT const &MediaID, MediaTimeT Position);
		void PlayInternal(HashT const &MediaID, MediaTimeT Position, uint64_t SystemTime, uint64_t Now);
		void LocalStopInternal(void);
		void StopInternal(void);
		bool IsPlayingInternal(void);

		static void VLCMediaEndCallback(libvlc_event_t const *Event, void *UserData);

		CallTransferType &CallTransfer; // Makes a call in the core's main thread

		Core Parent;

		EngineWrapper Engine;
		std::map<HashT, std::unique_ptr<MediaItem>> MediaLookup;
		MediaItem *Playing;
		MediaTimePercentT LastPosition;

		LatencyTracker Latencies;
};

enum class PlaylistColumns
{
	Track,
	Artist,
	Album,
	Title
};

enum struct PlayState { Deselected, Pause, Play };

struct PlaylistType
{
	struct PlaylistInfo
	{
		HashT Hash;
		PlayState State;
		OptionalT<uint16_t> Track;
		std::string Title;
		std::string Album;
		std::string Artist;
		PlaylistInfo(HashT const &Hash, decltype(State) const &State, OptionalT<uint16_t> const &Track, std::string const &Title, std::string const &Album, std::string const &Artist);
		PlaylistInfo(void);
	};
	protected:
		std::vector<PlaylistInfo> Playlist;
		OptionalT<size_t> Index;
	public:

	OptionalT<size_t> Find(HashT const &Hash);
	void AddUpdate(MediaInfo const &Item);
	void Remove(HashT const &Hash);
	bool Select(HashT const &Hash);
	OptionalT<bool> IsPlaying(void);
	OptionalT<HashT> GetID(size_t Row) const;
	OptionalT<HashT> GetCurrentID(void) const;
	OptionalT<PlaylistInfo> GetCurrent(void) const;
	std::vector<PlaylistInfo> const &GetItems(void) const;
	OptionalT<HashT> GetNextID(void) const;
	OptionalT<HashT> GetPreviousID(void) const;
	void Play(void);
	void Stop(void);
	void Shuffle(void);
	struct SortFactor
	{
		PlaylistColumns Column;
		bool Reverse;
		SortFactor(PlaylistColumns const Column, bool const Reverse);
	};
	void Sort(std::list<SortFactor> const &Factors);
};

#endif
