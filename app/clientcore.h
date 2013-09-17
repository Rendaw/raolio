#ifndef clientcore_h
#define clientcore_h

struct Core
{
	Core(bool Listen, std::string const &Host, uint16_t Port);
};

struct ClientCore
{
	ClientCore(bool Listen, std::string const &Host, uint16_t Port);
	~ClientCore(void);
	libvlc_instance_t *VLC;
	libvlc_media_player_t *VLCMediaPlayer;
	Core Event;
};

#endif
