#include "clientcore.h"

#include "error.h"

static std::string ExtractMeta(libvlc_media_t *Media, libvlc_meta_t const &MetaID, std::string const &Default = {})
{
	auto Out = libvlc_media_get_meta(Media, MetaID);
	if (!Out) return Default;
	return Out;
}

MediaItem::~MediaItem(void) { libvlc_media_release(VLCMedia); }

ExtraScopeItem::~ExtraScopeItem(void) {}

//ClientCore::ClientCore(bool Listen, std::string const &Host, uint16_t Port)
ClientCore::ClientCore(CallTransferType &CallTransfer) : CallTransfer(CallTransfer), PlaylistIndex(0)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw SystemError() << "Could not initialize libVLC.";
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw SystemError() << "Could not initialice libVLC media player.";
	libvlc_event_attach(libvlc_media_player_event_manager(VLCMediaPlayer), libvlc_MediaPlayerEndReached, VLCMediaEndCallback, this);

	TempPath = {bfs::temp_directory_path() / bfs::unique_path()};
}

ClientCore::~ClientCore(void)
{
	bfs::remove_all(TempPath);
	libvlc_media_player_release(VLCMediaPlayer);
	libvlc_release(VLC);
}

std::vector<std::unique_ptr<MediaItem>> const &ClientCore::GetPlaylist(void) const { return Media; }

void ClientCore::Command(std::string const &CommandString) {}

struct VLCParsedUserData : ExtraScopeItem
{
	VLCParsedUserData(ClientCore &Core, HashType const &Hash) : Core(Core), Hash(Hash) {}
	ClientCore &Core;
	HashType Hash;
};

void ClientCore::Add(std::array<uint8_t, 16> const &Hash, bfs::path const &Filename)
{
	if (MediaLookup.find(Hash) != MediaLookup.end()) return;
	auto *VLCMedia = libvlc_media_new_path(VLC, Filename.string().c_str());
	if (!VLCMedia)
	{
		if (LogCallback) LogCallback(String() << "Failed to open selected media, " << Filename << "; Removing from playlist.");
		return;
	}

	auto Item = new MediaItem{ false, Hash, Filename, {}, {}, {}, VLCMedia};
	Media.push_back(std::unique_ptr<MediaItem>(Item));
	//Media.push_back(make_unique<MediaItem>(false, Hash, Filename, {}, {}, Filename.filename().string(), VLCMedia));
	MediaLookup[Hash] = &*Media.back();
	if (MediaAddedCallback) MediaAddedCallback();

	auto CallbackData = new VLCParsedUserData{*this, Hash};
	ExtraScope.push_back(std::unique_ptr<ExtraScopeItem>{CallbackData});
	libvlc_event_attach(libvlc_media_event_manager(VLCMedia), libvlc_MediaParsedChanged, VLCMediaParsedCallback, CallbackData);
	libvlc_media_parse_async(VLCMedia);

	Item->Artist = ExtractMeta(VLCMedia, libvlc_meta_Artist);
	Item->Album = ExtractMeta(VLCMedia, libvlc_meta_Album);
	Item->Title = ExtractMeta(VLCMedia, libvlc_meta_Title, Filename.filename().string());

	if (Media.size() == 1) Select(0);
}

void ClientCore::Shuffle(void)
{
	auto Current = Media[PlaylistIndex].get();
	std::random_shuffle(Media.begin(), Media.end());
	for (size_t Index = 0; Index < Media.size(); ++Index)
	      if (Media[Index].get() == Current)
	      {
		      PlaylistIndex = Index;
		      break;
	      }
}

void ClientCore::Sort(void)
{
	auto Current = Media[PlaylistIndex].get();
	std::sort(Media.begin(), Media.end(),
		[](std::unique_ptr<MediaItem> const &Left, std::unique_ptr<MediaItem> const &Right)
		{
			if (Left->Album != Right->Album) return Left->Album < Right->Album;
			if (Left->Artist != Right->Artist) return Left->Artist < Right->Artist;
			return Left->Title < Right->Title;
		});
	for (size_t Index = 0; Index < Media.size(); ++Index)
	      if (Media[Index].get() == Current)
	      {
		      PlaylistIndex = Index;
		      break;
	      }
}

void ClientCore::SetVolume(float Volume) { libvlc_audio_set_volume(VLCMediaPlayer, static_cast<int>(Volume * 100)); }

void ClientCore::Seek(float Time) { libvlc_media_player_set_position(VLCMediaPlayer, Time); }

float ClientCore::GetTime(void) { return libvlc_media_player_get_position(VLCMediaPlayer); }

void ClientCore::PlayStop(void)
{
	if (!IsPlaying())
	{
		Media[PlaylistIndex]->Playing = true;
		libvlc_media_player_play(VLCMediaPlayer);
	}
	else
	{
		Media[PlaylistIndex]->Playing = false;
		libvlc_media_player_pause(VLCMediaPlayer);
	}
	if (MediaUpdatedCallback) MediaUpdatedCallback();
}

bool ClientCore::IsPlaying(void) { return libvlc_media_player_is_playing(VLCMediaPlayer); }

void ClientCore::Next(void)
{
	if (PlaylistIndex >= Media.size() - 1)
		Select(0);
	else Select(PlaylistIndex + 1);
}

void ClientCore::Previous(void)
{
	if (PlaylistIndex > 0)
		Select(PlaylistIndex - 1);
	else Select(Media.size() - 1);
}

void ClientCore::Select(size_t Which)
{
	assert(PlaylistIndex < Media.size());
	Media[PlaylistIndex]->Playing = false;
	if (MediaUpdatedCallback) MediaUpdatedCallback();

	assert(Which < Media.size());
	auto &NextMedia = *Media[Which];
	bool const Play = IsPlaying();
	if (LogCallback) LogCallback(String() << "Playing [" << Which << "] " << NextMedia.Title);
	libvlc_media_player_set_media(VLCMediaPlayer, NextMedia.VLCMedia);
	PlaylistIndex = Which;
	if (Play) PlayStop();
}

void ClientCore::VLCMediaEndCallback(libvlc_event_t const *Event, void *UserData)
{
	auto This = static_cast<ClientCore *>(UserData);
	This->CallTransfer([This](void)
	{
		if (This->PlaylistIndex >= This->Media.size() - 1)
		{
			// Re-prep song and reset position
			This->Select(This->PlaylistIndex);
			if (This->StoppedCallback) This->StoppedCallback();
		}
		else
		{
			This->Select(This->PlaylistIndex + 1);
			This->PlayStop();
		}
	});
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
		if (This.MediaUpdatedCallback) This.MediaUpdatedCallback();
	});
}
