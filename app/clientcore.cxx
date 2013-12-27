#include "clientcore.h"

#include "error.h"

#include <boost/locale.hpp>
#include <boost/regex.hpp>

static struct SetBoostLocaleStatic
{
	SetBoostLocaleStatic(void)
	{
		std::locale::global(boost::locale::generator().generate(""));
		boost::filesystem::path::imbue(std::locale());
	}
} SetBoostLocale;

EngineWrapper::EngineWrapper(void)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw ConstructionError() << Local("Could not initialize libVLC: ^0", libvlc_errmsg());
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw ConstructionError() << Local("Could not initialice libVLC media player: ^0", libvlc_errmsg());
}

EngineWrapper::~EngineWrapper(void)
{
	libvlc_media_player_release(VLCMediaPlayer);
	libvlc_release(VLC);
}

static std::string ExtractMeta(libvlc_media_t *Media, libvlc_meta_t const &MetaID, std::string const &Default = {})
{
	auto Out = libvlc_media_get_meta(Media, MetaID);
	if (!Out) return Default;
	return Out;
}

static void ExtractAllMeta(MediaInfo &Out, libvlc_media_t *Media)
{
	String TrackExtractor(ExtractMeta(Media, libvlc_meta_TrackNumber));
	int16_t Track = -1;
	TrackExtractor >> Track;
	if (Track >= 0) Out.Track = Track;
	else Out.Track = {};
	Out.Artist = ExtractMeta(Media, libvlc_meta_Artist, Out.Artist);
	Out.Album = ExtractMeta(Media, libvlc_meta_Album, Out.Album);
	Out.Title = ExtractMeta(Media, libvlc_meta_Title, Out.Title);
}

MediaItem::MediaItem(HashT const &Hash, bfs::path const &Filename, Optional<uint16_t> const &Track, std::string const &Artist, std::string const &Album, std::string const &Title, libvlc_media_t *VLCMedia) : MediaInfo{Hash, Filename, Track, Artist, Album, Title}, VLCMedia{VLCMedia} {}

MediaItem::~MediaItem(void) { libvlc_media_release(VLCMedia); }

ClientCore::ClientCore(float Volume) : CallTransfer(Parent), Parent{false}, Playing{nullptr}, LastPosition{0}
{
	libvlc_event_attach(libvlc_media_player_event_manager(Engine.VLCMediaPlayer), libvlc_MediaPlayerEndReached, VLCMediaEndCallback, this);
	libvlc_audio_set_volume(Engine.VLCMediaPlayer, static_cast<int>(Volume * 100));
	libvlc_audio_set_volume(Engine.VLCMediaPlayer, static_cast<int>(Volume * 100));

	Parent.LogCallback = [this](Core::LogPriority Priority, std::string const &Message)
	{
#ifdef NDEBUG
		if (Priority >= Core::Debug) return;
#endif
		if (LogCallback) LogCallback(Message);
	};

	Parent.ChatCallback = [this](std::string const &Message) { if (LogCallback) LogCallback(Message); };
	Parent.AddCallback = [this](HashT const &Hash, bfs::path const &Filename, std::string const &DefaultTitle)
		{ AddInternal(Hash, Filename, DefaultTitle); };
	Parent.RemoveCallback = [this](HashT const &Hash)
		{ RemoveInternal(Hash); };
	Parent.ClockCallback = [this](uint64_t InstanceID, uint64_t const &SystemTime)
		{ Latencies.Add(InstanceID, SystemTime); };
	Parent.PlayCallback = [this](HashT const &MediaID, MediaTimeT MediaTime, uint64_t const &SystemTime)
		{ auto const Now = GetNow(); PlayInternal(MediaID, MediaTime, SystemTime, Now); };
	Parent.StopCallback = [this](void)
		{ StopInternal(); };
}

void ClientCore::Open(bool Listen, std::string const &Host, uint16_t Port)
	{ Parent.Open(Listen, Host, Port); }

void ClientCore::Add(HashT const &Hash, size_t Size, bfs::path const &Filename)
{
	CallTransfer([=](void)
	{
		Parent.Add(Hash, Size, Filename);
		AddInternal(Hash, Filename, Filename.filename().string());
	});
}

void ClientCore::Remove(HashT const &Hash)
{
	CallTransfer([=](void)
	{
		Parent.Remove(Hash);
		RemoveInternal(Hash);
	});
}

void ClientCore::RemoveAll(void)
{
	CallTransfer([=](void)
	{
		while (!MediaLookup.empty())
			Remove(MediaLookup.begin()->first);
	});
}

void ClientCore::SetVolume(float Volume)
	{ CallTransfer([=](void) { SetVolumeInternal(Volume); }); }

void ClientCore::GetTime(void)
{
	CallTransfer([=](void)
	{
		if (SeekCallback)
		{
			float Duration = Playing ? libvlc_media_get_duration(Playing->VLCMedia) / 1000.0f : 0.0f;
			SeekCallback(GetTimeInternal(), Duration);
		}
	});
}

void ClientCore::Play(HashT const &MediaID, MediaTimeT Position)
	{ CallTransfer([=](void) { LocalPlayInternal(MediaID, Position); }); }

void ClientCore::Play(HashT const &MediaID, float Position)
{
	CallTransfer([=](void)
	{
		auto Media = MediaLookup.find(MediaID);
		if (Media == MediaLookup.end()) return;
		MediaTimeT TotalLength{libvlc_media_get_duration(Media->second->VLCMedia)};
		LocalPlayInternal(MediaID, MediaTimeT(Position * StrictCast(TotalLength, float)));
		LastPosition = MediaTimePercentT{Position};
	});
}

void ClientCore::Play(void)
{
	CallTransfer([&](void)
	{
		if (!Playing) return;
		MediaTimeT Position{libvlc_media_player_get_time(Engine.VLCMediaPlayer)};
		LocalPlayInternal(Playing->Hash, Position);
	});
}

void ClientCore::Stop(void)
	{ CallTransfer([&](void) { if (!Playing) return; LocalStopInternal(); }); }

void ClientCore::Chat(std::string const &Message)
{
	CallTransfer([=](void) { Parent.Chat(Message); });
}

struct VLCParsedUserData
{
	VLCParsedUserData(ClientCore &Core, HashT const &Hash) : Core(Core), Hash(Hash) {}
	ClientCore &Core;
	HashT Hash;
};

void ClientCore::AddInternal(HashT const &Hash, bfs::path const &Filename, std::string const &DefaultTitle)
{
	if (MediaLookup.find(Hash) != MediaLookup.end()) return;
	auto *VLCMedia = libvlc_media_new_path(Engine.VLC, Filename.string().c_str());
	if (!VLCMedia)
	{
		if (LogCallback) LogCallback(Local("Failed to open selected media, ^0: ^1", Filename, libvlc_errmsg()));
		return;
	}

	auto Item = new MediaItem{Hash, Filename, {}, {}, {}, DefaultTitle, VLCMedia};
	MediaLookup[Hash] = std::unique_ptr<MediaItem>(Item);

	if (libvlc_media_is_parsed(VLCMedia))
		ExtractAllMeta(*Item, VLCMedia);
	else
	{
		libvlc_event_attach(libvlc_media_event_manager(VLCMedia), libvlc_MediaParsedChanged, VLCMediaParsedCallback, new VLCParsedUserData{*this, Hash});
		libvlc_media_parse_async(VLCMedia);
	}

	if (AddCallback) AddCallback(*Item);

	Core::PlayStatus LastPlayStatus = Parent.GetPlayStatus();
	if (LastPlayStatus.Playing && (Hash == LastPlayStatus.MediaID))
		PlayInternal(LastPlayStatus.MediaID, LastPlayStatus.MediaTime, LastPlayStatus.SystemTime, GetNow());
}

void ClientCore::RemoveInternal(HashT const &Hash)
{
	auto Found = MediaLookup.find(Hash);
	if (Found == MediaLookup.end()) return;
	if (RemoveCallback) RemoveCallback(Hash);
	MediaLookup.erase(Found);
}

void ClientCore::SetVolumeInternal(float Volume) { libvlc_audio_set_volume(Engine.VLCMediaPlayer, static_cast<int>(Volume * 100)); }

float ClientCore::GetTimeInternal(void)
{
	auto const NewPosition = libvlc_media_player_get_position(Engine.VLCMediaPlayer);
	if (NewPosition > 0) LastPosition = MediaTimePercentT{NewPosition};
	return *LastPosition;
}

void ClientCore::LocalPlayInternal(HashT const &MediaID, MediaTimeT Position)
{
	auto Start = Latencies.Expected() + GetNow();
	Parent.Play(MediaID, Position, Start);
	PlayInternal(MediaID, Position, Start, GetNow());
}

void ClientCore::PlayInternal(HashT const &MediaID, MediaTimeT Position, uint64_t SystemTime, uint64_t Now)
{
	auto Media = MediaLookup.find(MediaID);
	if (Media == MediaLookup.end()) return;
	libvlc_media_player_set_media(Engine.VLCMediaPlayer, Media->second->VLCMedia);
	float StartTime = StrictCast(Position, float) / libvlc_media_player_get_length(Engine.VLCMediaPlayer);
	if (SelectCallback) SelectCallback(MediaID);
	if (Now >= SystemTime)
	{
		libvlc_media_player_play(Engine.VLCMediaPlayer);
		libvlc_media_player_set_time(Engine.VLCMediaPlayer, *Position + (Now - SystemTime));
	}
	else
	{
		libvlc_media_player_pause(Engine.VLCMediaPlayer);
		Parent.Schedule((float)(SystemTime - Now /* - Epsilon */) / 1000.0f, [this, Position, StartTime](void)
		{
			libvlc_media_player_play(Engine.VLCMediaPlayer);
			libvlc_media_player_set_time(Engine.VLCMediaPlayer, *Position);
		});
	}
	Playing = Media->second.get();
	if (PlayCallback) PlayCallback();
}

void ClientCore::LocalStopInternal(void)
{
	Parent.Stop();
	StopInternal();
}

void ClientCore::StopInternal(void)
{
	libvlc_media_player_pause(Engine.VLCMediaPlayer);
	if (StopCallback) StopCallback();
}

bool ClientCore::IsPlayingInternal(void) { return libvlc_media_player_is_playing(Engine.VLCMediaPlayer); }

void ClientCore::VLCMediaEndCallback(libvlc_event_t const *Event, void *UserData)
{
	auto This = static_cast<ClientCore *>(UserData);
	if (This->EndCallback) This->EndCallback();
}

void ClientCore::VLCMediaParsedCallback(libvlc_event_t const *Event, void *UserData)
{
	auto Data = static_cast<VLCParsedUserData *>(UserData);
	Data->Core.CallTransfer([Data](void)
	{
		auto Media = Data->Core.MediaLookup.find(Data->Hash);
		if (Media == Data->Core.MediaLookup.end()) return;
		ExtractAllMeta(*Media->second, Media->second->VLCMedia);
		if (Data->Core.UpdateCallback) Data->Core.UpdateCallback(*Media->second.get());
		delete Data;
	});
}

PlaylistType::PlaylistInfo::PlaylistInfo(HashT const &Hash, decltype(State) const &State, Optional<uint16_t> const &Track, std::string const &Title, std::string const &Album, std::string const &Artist) : Hash(Hash), State{State}, Track{Track}, Title{Title}, Album{Album}, Artist{Artist} {}
PlaylistType::PlaylistInfo::PlaylistInfo(void) {}

Optional<size_t> PlaylistType::Find(HashT const &Hash)
{
	for (size_t Index = 0; Index < Playlist.size(); ++Index) if (Playlist[Index].Hash == Hash) return Index;
	return {};
}

void PlaylistType::AddUpdate(MediaInfo const &Item)
{
	auto Found = Find(Item.Hash);
	if (!Found)
	{
		Playlist.emplace_back(Item.Hash, PlayState::Deselected, Item.Track, Item.Title, Item.Album, Item.Artist);
	}
	else
	{
		Playlist[*Found].Track = Item.Track;
		Playlist[*Found].Title = Item.Title;
		Playlist[*Found].Album = Item.Album;
		Playlist[*Found].Artist = Item.Artist;
	}
}

void PlaylistType::Remove(HashT const &Hash)
{
	auto Found = Find(Hash);
	if (!Found) return;
	if (Index && (*Found == *Index)) Index = {};
	Playlist.erase(Playlist.begin() + *Found);
}

bool PlaylistType::Select(HashT const &Hash)
{
	auto Found = Find(Hash);
	if (!Found) return true;
	if (Index)
	{
		Playlist[*Index].State = PlayState::Deselected;
	}
	bool Out = Index == Found;
	Index = *Found;
	Playlist[*Index].State = PlayState::Pause;
	return Out;
}

Optional<bool> PlaylistType::IsPlaying(void)
{
	if (!Index) return {};
	return Playlist[*Index].State == PlayState::Play;
}

Optional<HashT> PlaylistType::GetID(size_t Row) const
{
	if (Row >= Playlist.size()) return {};
	return Playlist[Row].Hash;
}

Optional<HashT> PlaylistType::GetCurrentID(void) const
{
	if (!Index) return {};
	return Playlist[*Index].Hash;
}

Optional<PlaylistType::PlaylistInfo> PlaylistType::GetCurrent(void) const
{
	if (!Index) return {};
	return Playlist[*Index];
}

std::vector<PlaylistType::PlaylistInfo> const &PlaylistType::GetItems(void) const { return Playlist; }

Optional<HashT> PlaylistType::GetNextID(void) const
{
	if (!Index)
	{
		if (Playlist.empty()) return {};
		else return Playlist.front().Hash;
	}
	else
	{
		if (*Index + 1 >= Playlist.size())
			return Playlist.front().Hash;
		return Playlist[*Index + 1].Hash;
	}
}

Optional<HashT> PlaylistType::GetPreviousID(void) const
{
	if (!Index)
	{
		if (Playlist.empty()) return {};
		else return Playlist.back().Hash;
	}
	else
	{
		if (*Index == 0)
			return Playlist.back().Hash;
		return Playlist[*Index - 1].Hash;
	}
}

void PlaylistType::Play(void)
{
	if (!Index) return;
	Playlist[*Index].State = PlayState::Play;
}

void PlaylistType::Stop(void)
{
	if (!Index) return;
	Playlist[*Index].State = PlayState::Pause;
}

void PlaylistType::Shuffle(void)
{
	HashT CurrentID;
	if (Index)
	{
		assert(*Index < Playlist.size());
		CurrentID = Playlist[*Index].Hash;
	}
	std::random_shuffle(Playlist.begin(), Playlist.end());
	auto Found = Find(CurrentID);
	if (Found) Index = *Found;
}

PlaylistType::SortFactor::SortFactor(PlaylistColumns const Column, bool const Reverse) : Column{Column}, Reverse{Reverse} {}

void PlaylistType::Sort(std::list<SortFactor> const &Factors)
{
	HashT CurrentID;
	if (Index)
	{
		assert(*Index < Playlist.size());
		CurrentID = Playlist[*Index].Hash;
	}
	std::stable_sort(Playlist.begin(), Playlist.end(), [&Factors](PlaylistType::PlaylistInfo const &First, PlaylistType::PlaylistInfo const &Second)
	{
		for (auto &Factor : Factors)
		{
			auto const Fix = [&Factor](bool const Verdict) { if (Factor.Reverse) return !Verdict; return Verdict; };
			switch (Factor.Column)
			{
				case PlaylistColumns::Track:
					if (First.Track == Second.Track) continue;
					if (!First.Track) return Fix(true);
					if (!Second.Track) return Fix(false);
					return Fix(*First.Track < *Second.Track);
				case PlaylistColumns::Title:
					if (First.Title == Second.Title) continue;
					return Fix(First.Title < Second.Title);
				case PlaylistColumns::Album:
					if (First.Album == Second.Album) continue;
					return Fix(First.Album < Second.Album);
				case PlaylistColumns::Artist:
					if (First.Artist == Second.Artist) continue;
					return Fix(First.Artist < Second.Artist);
				default: assert(false); continue;
			}
		}
		return false;
	});
	auto Found = Find(CurrentID);
	if (Found) Index = *Found;
}
