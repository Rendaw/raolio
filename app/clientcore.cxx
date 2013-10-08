#include "clientcore.h"

#include "error.h"

EngineWrapper::EngineWrapper(void)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw SystemError() << "Could not initialize libVLC.";
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw SystemError() << "Could not initialice libVLC media player.";
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

MediaItem::~MediaItem(void) { libvlc_media_release(VLCMedia); }

ExtraScopeItem::~ExtraScopeItem(void) {}

ClientCore::ClientCore(std::string const &Host, uint16_t Port) : CallTransfer(Parent), Playing{nullptr}
{
	libvlc_event_attach(libvlc_media_player_event_manager(Engine.VLCMediaPlayer), libvlc_MediaPlayerEndReached, VLCMediaEndCallback, this);

	Parent.ChatCallback = [this](std::string const &Message) { if (LogCallback) LogCallback(Message); };
	Parent.AddCallback = [this](HashType const &Hash, bfs::path const &Filename)
		{ AddInternal(Hash, Filename); };
	Parent.ClockCallback = [this](uint64_t InstanceID, uint64_t const &SystemTime)
		{ Latencies.Add(InstanceID, SystemTime); };
	Parent.PlayCallback = [this](HashType const &MediaID, uint64_t MediaTime, uint64_t const &SystemTime)
		{ auto const Now = GetNow(); PlayInternal(MediaID, MediaTime, SystemTime, Now); };
	Parent.StopCallback = [this](void)
		{ StopInternal(); };

	Parent.Open(false, Host, Port);
}

void ClientCore::Add(HashType const &Hash, bfs::path const &Filename)
{
	CallTransfer([&, Hash, Filename](void)
	{
		Parent.Add(Hash, Filename);
		AddInternal(Hash, Filename);
	});
}

void ClientCore::SetVolume(float Volume)
	{ CallTransfer([&, Volume](void) { SetVolumeInternal(Volume); }); }

void ClientCore::GetTime(void)
	{ CallTransfer([&](void) { if (SeekCallback) SeekCallback(GetTimeInternal()); }); }

void ClientCore::Play(HashType const &MediaID, uint64_t Position)
	{ CallTransfer([&, MediaID, Position](void) { LocalPlayInternal(MediaID, Position); }); }

void ClientCore::Play(HashType const &MediaID, float Position)
{
	CallTransfer([&, MediaID, Position](void)
	{
		auto Media = MediaLookup.find(MediaID);
		if (Media == MediaLookup.end()) return;
		uint64_t TotalLength = libvlc_media_get_duration(Media->second->VLCMedia);
		LocalPlayInternal(MediaID, Position * TotalLength);
	});
}

void ClientCore::Play(void)
{
	CallTransfer([&](void)
	{
		if (!Playing) return;
		uint64_t Position = libvlc_media_player_get_time(Engine.VLCMediaPlayer);
		LocalPlayInternal(Playing->Hash, Position);
	});
}

void ClientCore::Stop(void)
	{ CallTransfer([&](void) { LocalStopInternal(); }); }

void ClientCore::Chat(std::string const &Message)
	{ CallTransfer([&](void) { Parent.Chat(Message); }); }

struct VLCParsedUserData : ExtraScopeItem
{
	VLCParsedUserData(ClientCore &Core, HashType const &Hash) : Core(Core), Hash(Hash) {}
	ClientCore &Core;
	HashType Hash;
};

void ClientCore::AddInternal(HashType const &Hash, bfs::path const &Filename)
{
	if (MediaLookup.find(Hash) != MediaLookup.end()) return;
	auto *VLCMedia = libvlc_media_new_path(Engine.VLC, Filename.string().c_str());
	if (!VLCMedia)
	{
		if (LogCallback) LogCallback(String() << "Failed to open selected media, " << Filename << "; Removing from playlist.");
		return;
	}

	auto Item = new MediaItem{Hash, Filename, {}, {}, {}, VLCMedia};
	MediaLookup[Hash] = std::unique_ptr<MediaItem>(Item);

	auto CallbackData = new VLCParsedUserData{*this, Hash};
	ExtraScope.push_back(std::unique_ptr<ExtraScopeItem>{CallbackData});
	libvlc_event_attach(libvlc_media_event_manager(VLCMedia), libvlc_MediaParsedChanged, VLCMediaParsedCallback, CallbackData);
	libvlc_media_parse_async(VLCMedia);

	Item->Artist = ExtractMeta(VLCMedia, libvlc_meta_Artist);
	Item->Album = ExtractMeta(VLCMedia, libvlc_meta_Album);
	Item->Title = ExtractMeta(VLCMedia, libvlc_meta_Title, Filename.filename().string());

	if (AddCallback) AddCallback(Item);
}

void ClientCore::SetVolumeInternal(float Volume) { libvlc_audio_set_volume(Engine.VLCMediaPlayer, static_cast<int>(Volume * 100)); }

void ClientCore::SeekInternal(float Time) { libvlc_media_player_set_position(Engine.VLCMediaPlayer, Time); }

float ClientCore::GetTimeInternal(void) { return libvlc_media_player_get_position(Engine.VLCMediaPlayer); }

void ClientCore::LocalPlayInternal(HashType const &MediaID, uint64_t Position)
{
	auto Start = Latencies.Expected() + GetNow();
	Parent.Play(MediaID, Position, Start);
	PlayInternal(MediaID, Position, Start, GetNow());
}

void ClientCore::PlayInternal(HashType const &MediaID, uint64_t Position, uint64_t SystemTime, uint64_t Now)
{
	auto Media = MediaLookup.find(MediaID);
	if (Media == MediaLookup.end()) return;
	libvlc_media_player_set_media(Engine.VLCMediaPlayer, Media->second->VLCMedia);
	if (SelectCallback) SelectCallback(MediaID);
	auto const Delay = SystemTime - Now;
	if (Now >= SystemTime)
	{
		libvlc_media_player_set_time(Engine.VLCMediaPlayer, Position + Now - SystemTime);
		libvlc_media_player_play(Engine.VLCMediaPlayer);
		if (PlayCallback) PlayCallback();
	}
	else if (Position > Delay)
	{
		libvlc_media_player_set_time(Engine.VLCMediaPlayer, Position - Delay);
		libvlc_media_player_play(Engine.VLCMediaPlayer);
		if (PlayCallback) PlayCallback();
	}
	else
	{
		libvlc_media_player_set_time(Engine.VLCMediaPlayer, Position);
		Parent.Schedule((float)(Delay /* - Epsilon */) / 1000.0f, [this](void)
		{
			libvlc_media_player_play(Engine.VLCMediaPlayer);
			if (PlayCallback) PlayCallback();
		});
	}
	Playing = Media->second.get();
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
	auto &This = Data->Core;
	This.CallTransfer([&This, Data](void)
	{
		auto Media = This.MediaLookup.find(Data->Hash);
		if (Media == This.MediaLookup.end()) return;
		Media->second->Artist = ExtractMeta(Media->second->VLCMedia, libvlc_meta_Artist);
		Media->second->Album = ExtractMeta(Media->second->VLCMedia, libvlc_meta_Album);
		Media->second->Title = ExtractMeta(Media->second->VLCMedia, libvlc_meta_Title);
		if (This.UpdateCallback) This.UpdateCallback(Media->second.get());
	});
}
