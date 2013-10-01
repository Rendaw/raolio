#include "core.h"

auto Clock = std::chrono::high_resolution_clock;

struct CoreConnection : Network::Connection
{
	struct RequestInfo
	{
		HashType Hash;
		size_t Size;
		size_t 
	};
	std::vector<HashType> Requests;
};

Core::Core(bool Listen, std::string const &Host, uint8_t Port) :
	TempPath{bfs::temp_directory_path() / bfs::unique_path()},
	ID{std::uniform_int_distribution<uint64_t>{}(std::mt19937{})},
	Net<CoreConnection>
	{
		[](std::string const &Host, uint16_t Port, int Socket) // Create connection
			{ return new CoreConnection{Host, Port, Socket}; },
		[this](Connection &Info) // Idle write
		{
			// TODO
			auto &BroadcastIndex = Info->BroadcastIndex;
			if (BroadcastIndex < OrderedMedia.size())
			{
				Net.Reply(NP1V1Prepare, Info, OrderedMedia[BroadcastIndex]->ID, OrderedMedia[BroadcastIndex]->Size);
				++BroadcastIndex;
				return true;
			}
			return false;
		},
		[this](Connection &Info, uint64_t const &InstanceID, uint64_t const &SystemTime) // Clock
		{
			Net.Forward(NP1V1Clock, Info, InstanceID, SystemTime);
			if (ClockCallback) ClockCallback(InstanceID, SystemTime);
		},
		[this](Connection &Info, HashType const &MediaID, uint64_t const &Size) // Prepare
		{
			Net.Forward(NP1V1Prepare, Info, MediaID, Size);
			bool Found = Library.Prepare(MediaID, Size);
			if (Found) return;
			Info.Requests.push_back(MediaID);
		},
		[this](Connection &Info, HashType const &MediaID, uint64_t const &Chunk) // Request
		{
			auto Out = Library.Get(MediaID, Chunk);
			if (Out.empty()) return;
			Net.Reply(NP1V1Data, Info, MediaID, Chunk, Out);
		},
		[this](Connection &Info, HashType const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes) // Data
		{
			Library.Write(MediaID, Chunk * ChunkSize, Bytes);
		},
		[this](Connection &Info, HashType const &MediaID, uint64_t const &MediaTime, uint64_t const &SystemTime) // Play
		{
			Net.Forward(NP1V1Play, Info, MediaID, MediaTime, SystemTime);
			LastPlaying = true;
			LastMediaTime = MediaTime;
			LastSystemTime = SystemTime;
			if (PlayCallback) PlayCallback(MediaTime, SystemTime);
		},
		[this](Connection &Info) // Stop
		{
			Net.Forward(NP1V1Stop, Info);
			LastPlaying = false;
			LastMediaTime = MediaTime;
			LastSystemTime = SystemTime;
			if (StopCallback) StopCallback();
		},
		[this](Connection &Info, std::string const &Message) // Chat
		{
			Net.Forward(NP1V1Chat, Info, Message);
			if (ChatCallback) ChatCallback(Message);
		},
	}
{
	Net.OpenTimer(10, [this](void) // Timer
	{
		Net.Broadcast(NP1V1Clock, ID, SystemTime);
	});
	Net.Open(Listen ? new CustomListener<CoreConnection>(Host, Port) : new CoreConnection(Host, Port));
}

Core::~Core(void)
{
	bfs::remove_all(TempPath);
}
