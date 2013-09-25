#include "core.h"

auto Clock = std::chrono::high_resolution_clock;

struct CoreConnection : Network::Connection
{
	bool IdleWrite(void)
	{
	}
};

Core::Core(bool Listen, std::string const &Host, uint8_t Port) :
	TempPath{bfs::temp_directory_path() / bfs::unique_path()},
	ID{std::uniform_int_distribution<uint64_t>{}(std::mt19937{})},
	Net
	{
		[this](SocketInfo *Info) // Idle write
		{
			auto &BroadcastIndex = Info->BroadcastIndex;
			if (BroadcastIndex < OrderedMedia.size())
			{
				Net.Reply(NP1V1Prepare, Info, OrderedMedia[BroadcastIndex]->ID, OrderedMedia[BroadcastIndex]->Size);
				++BroadcastIndex;
				return true;
			}
			return false;
		},
		[this](SocketInfo *Info) // Timer
		{
			Net.Broadcast(NP1V1Clock, ID, SystemTime);
		},
		[this](SocketInfo *Info, uint64_t const &InstanceID, uint64_t const &SystemTime) // Clock
		{
			Net.Forward(NP1V1Clock, Info, InstanceID, SystemTime);
			if (ClockCallback) ClockCallback(InstanceID, SystemTime);
		},
		[this](SocketInfo *Info, HashType const &MediaID, uint64_t const &Size) // Prepare
		{
			Net.Forward(NP1V1Prepare, Info, MediaID, Size);
			
			auto Found = Media.find(MediaID);
			if (Found != Media.end()) return;
			
			Media[MediaID] = new MediaItem{TempPath / HashToString(MediaID), Size};
			OrderedMedia.push_back(
			Info->ExtraData->
		},
		[this](SocketInfo *Info, HashType const &MediaID, uint64_t const &Chunk) // Request
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			auto Found = Media.find(MediaID);
			if (Found == Media.end()) return;
			if (Found->second.Filename.empty()) return;
			if (!Found->second.Pieces.Has(Chunk)) return;
			Net.Reply(NP1V1Data, Info, Chunk, LibraryCache.Read(Found->second.Filename, Chunk * ChunkSize, ChunkSize));
		},
		[this](SocketInfo *Info, HashType const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes) // Data
			{ LibraryCache.Write(MediaID, Chunk * ChunkSize, Bytes); },
		[this](SocketInfo *Info, HashType const &MediaID, uint64_t const &MediaTime, uint64_t const &SystemTime) // Play
		{
			Net.Forward(NP1V1Play, Info, MediaID, MediaTime, SystemTime);
			std::lock_guard<std::mutex> Lock(Mutex);
			LastPlaying = true;
			LastMediaTime = MediaTime;
			LastSystemTime = SystemTime;
		},
		[this](SocketInfo *Info) // Stop
		{
			Net.Forward(NP1V1Stop, Info);
			std::lock_guard<std::mutex> Lock(Mutex);
			LastPlaying = false;
			LastMediaTime = MediaTime;
			LastSystemTime = SystemTime;
			if (StopCallback) StopCallback();
		},
		[this](SocketInfo *Info, std::string const &Message) // Chat
		{
			Net.Forward(NP1V1Chat, Info, Message);
			if (ChatCallback) ChatCallback(Message);
		},
	}
{
	Net.Open(Listen ? new CustomListener<CoreConnection>(Host, Port) : new CoreConnection(Host, Port));
}

Core::~Core(void)
{
	bfs::remove_all(TempPath);
}
