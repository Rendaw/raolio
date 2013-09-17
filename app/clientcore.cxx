#include "clientcore.h"

#include "error.h"
#include "shared.h"

//ClientCore::ClientCore(bool Listen, std::string const &Host, uint16_t Port)
ClientCore::ClientCore(void)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw SystemError() << "Could not initialize libVLC.";
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw SystemError() << "Could not initialice libVLC media player.";

	TempPath = {bfs::temp_directory_path() / bfs::unique_path()};
}

ClientCore::~ClientCore(void)
{
	bfs::remove_all(TempPath);
	libvlc_media_player_release(VLCMediaPlayer);
	libvlc_release(VLC);
}

std::vector<MediaItem> const &ClientCore::GetPlaylist(void) const { return Media; }

void ClientCore::Command(std::string const &CommandString) {}

void ClientCore::Add(std::array<uint8_t, 16> const &Hash, std::string const &Filename)
{
	if (MediaLookup.find(Hash) != MediaLookup.end()) return;
	Media.push_back({Hash, Filename});
	MediaLookup[Hash] = Filename;

	if (Media.size() == 1) Select(0);
}

void ClientCore::Shuffle(void) { std::random_shuffle(Media.begin(), Media.end()); }

void ClientCore::Sort(void) { std::sort(Media.begin(), Media.end()); }

void ClientCore::SetVolume(float Volume) { libvlc_audio_set_volume(VLCMediaPlayer, static_cast<int>(Volume * 100)); }

void ClientCore::Seek(float Time) { libvlc_media_player_set_position(VLCMediaPlayer, Time); }

float ClientCore::GetTime(void) { return libvlc_media_player_get_position(VLCMediaPlayer); }

void ClientCore::PlayStop(void)
{
	if (!IsPlaying())
		libvlc_media_player_play(VLCMediaPlayer);
	else libvlc_media_player_pause(VLCMediaPlayer);
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
	assert(Which < Media.size());
	bool const Play = IsPlaying();
	auto OpenedMedia = libvlc_media_new_path(VLC, Media[Which].Filename.c_str());
	if (!OpenedMedia)
	{
		std::cout << "Fail" << std::endl;
		if (LogCallback) LogCallback(String() << "Failed to open selected media, " << Media[Which].Filename << "; Removing from playlist.");
		Media.erase(Media.begin() + (long)Which);
		return;
	}
	libvlc_media_player_set_media(VLCMediaPlayer, OpenedMedia);
	libvlc_media_release(OpenedMedia);
	if (Play) PlayStop();
}
