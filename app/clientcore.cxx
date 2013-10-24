#include "clientcore.h"

#include "error.h"

EngineWrapper::EngineWrapper(void)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw ConstructionError() << "Could not initialize libVLC.";
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw ConstructionError() << "Could not initialice libVLC media player.";
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
	Out.Artist = ExtractMeta(Media, libvlc_meta_Artist);
	Out.Album = ExtractMeta(Media, libvlc_meta_Album);
	Out.Title = ExtractMeta(Media, libvlc_meta_Title);
}

MediaItem::MediaItem(HashT const &Hash, bfs::path const &Filename, Optional<uint16_t> const &Track, std::string const &Artist, std::string const &Album, std::string const &Title, libvlc_media_t *VLCMedia) : MediaInfo{Hash, Filename, Track, Artist, Album, Title}, VLCMedia{VLCMedia} {}

MediaItem::~MediaItem(void) { libvlc_media_release(VLCMedia); }

ClientCore::ClientCore(std::string const &Handle, float Volume) : CallTransfer(Parent), Parent{false}, Playing{nullptr}, LastPosition{0}, Handle{Handle}
{
	libvlc_event_attach(libvlc_media_player_event_manager(Engine.VLCMediaPlayer), libvlc_MediaPlayerEndReached, VLCMediaEndCallback, this);
	libvlc_audio_set_volume(Engine.VLCMediaPlayer, static_cast<int>(Volume * 100));

	Parent.LogCallback = [this](Core::LogPriority Priority, std::string const &Message)
	{
#ifdef NDEBUG
		if (Priority >= Core::Debug) return;
#endif
		if (LogCallback) LogCallback(Message);
	};

	Parent.ChatCallback = [this](std::string const &Message) { if (LogCallback) LogCallback(Message); };
	Parent.AddCallback = [this](HashT const &Hash, bfs::path const &Filename)
		{ AddInternal(Hash, Filename); };
	Parent.ClockCallback = [this](uint64_t InstanceID, uint64_t const &SystemTime)
		{ Latencies.Add(InstanceID, SystemTime); };
	Parent.PlayCallback = [this](HashT const &MediaID, MediaTimeT MediaTime, uint64_t const &SystemTime)
		{ auto const Now = GetNow(); PlayInternal(MediaID, MediaTime, SystemTime, Now); };
	Parent.StopCallback = [this](void)
		{ StopInternal(); };
}

void ClientCore::Open(bool Listen, std::string const &Host, uint16_t Port)
	{ Parent.Open(false, Host, Port); }

void ClientCore::Add(HashT const &Hash, size_t Size, bfs::path const &Filename)
{
	CallTransfer([=](void)
	{
		Parent.Add(Hash, Size, Filename);
		AddInternal(Hash, Filename);
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
	std::string QualifiedMessage = Handle + ": " + Message;
	CallTransfer([=](void) { Parent.Chat(QualifiedMessage); });
}

struct VLCParsedUserData
{
	VLCParsedUserData(ClientCore &Core, HashT const &Hash) : Core(Core), Hash(Hash) {}
	ClientCore &Core;
	HashT Hash;
};

void ClientCore::AddInternal(HashT const &Hash, bfs::path const &Filename)
{
	if (MediaLookup.find(Hash) != MediaLookup.end()) return;
	auto *VLCMedia = libvlc_media_new_path(Engine.VLC, Filename.string().c_str());
	if (!VLCMedia)
	{
		if (LogCallback) LogCallback(String() << "Failed to open selected media, " << Filename << "; Removing from playlist.");
		// TODO remove from playlist
		return;
	}

	auto Item = new MediaItem{Hash, Filename, {}, {}, {}, {}, VLCMedia};
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
	auto const Delay = SystemTime - Now;
	if (Now >= SystemTime)
	{
		libvlc_media_player_play(Engine.VLCMediaPlayer);
		libvlc_media_player_set_time(Engine.VLCMediaPlayer, *Position);
	}
	else
	{
		libvlc_media_player_pause(Engine.VLCMediaPlayer);
		Parent.Schedule((float)(Delay /* - Epsilon */) / 1000.0f, [this, Position, StartTime](void)
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
