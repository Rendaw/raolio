#include "clientcore.h"

ClientCore::ClientCore(bool Listen, std::string const &Host, uint16_t Port)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw SystemError("Could not initialize libVLC.");
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw SystemError("Could not initialice libVLC media player.");
	
	TempPath = {bfs::temp_directory_path() / bfs::unique_path()};
}


ClientCore::~ClientCore(void)
{
	bfs::remove_all(TempPath);
	libvlc_media_player_release(VLCMediaPlayer);
	libvlc_release(VLC);
}

void ClientCore::Add(std::array<uint8_t, 16> const &Hash, std::string const &Filename)
{
	if (MediaMap.find(Hash) != MediaMap.end()) return;
	Media.push_back({Hash, Filename});
	MediaMap[Hash] = Filename;
}

Core::Core(std::string const &Host, uint16_t Port)
{
	libvlc_media_player_new(
}


