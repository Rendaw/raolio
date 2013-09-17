#include "clientcore.h"

ClientCore::ClientCore(bool Listen, std::string const &Host, uint16_t Port)
{
	VLC = libvlc_new(0, nullptr);
	if (!VLC) throw SystemError("Could not initialize libVLC.");
	VLCMediaPlayer = libvlc_media_player_new(VLC);
	if (!VLCMediaPlayer) throw SystemError("Could not initialice libVLC media player.");


ClientCore::~ClientCore(void)
{
	libvlc_media_player_release(VLCMediaPlayer);
	libvlc_release(VLC);
}

Core::Core(std::string const &Host, uint16_t Port)
{
	libvlc_media_player_new(
}


