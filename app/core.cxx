#include "core.h"

auto Clock = std::chrono::high_resolution_clock;

struct CoreConnection : Network::Connection
{
	struct MediaInfo
	{
		HashType ID;
		size_t Size;
	};
	
	std::queuer<MediaInfo> Announce;
	
	struct
	{
		HashType ID;
		size_t Size;
		FilePieces Pieces;
		std::time_point LastResponse;
		bfs::path Path;
		std::fstream File;
	} Request;
	std::vector<MediaInfo> PendingRequests;
	struct FinishedMedia
	{
		HashType ID;
		size_t Size;
		bfs::path Path;
	};
	
	struct
	{
		HashType ID;
		std::fstream File;
		size_t Chunk;
	} DataStatus;
	
	Optional<FinishedMedia> ReceiveData(HashType const &ID, size_t const &Chunk, std::vector<uint8_t> const &Bytes)
	{
		if (MediaID != Request.ID) return {};
		if (Chunk != Request.Pieces.Next()) return {};
		assert(Request.File);
		assert(Request.File.tellp() == Chunk * ChunkSize);
		if (Bytes.size() < ChunkSize) return {}; // Probably an error condition
		Request.Pieces.Set(Chunk);
		Request.File.write(&Bytes[0], ChunkSize);
		Request.LastResponse = Clock::now();
		if (!Request.Pieces.Finished()) return {};
		
		Request.File.close();
		FinishedMedia Out{Request.ID, Request.Size, Request.Path};
		
		if (!PendingRequests.empty())
		{
			Request.ID = PendingRequests.front().ID;
			Request.Size = PendingRequests.front().Size;
			Request.Pieces = {};
			Request.Path = Root / FormatHash(Request.ID);
			Request.File = {Path, std::fstream::out};
			Send(NP1V1Request, Request.ID, Request.Pieces.Next());
		}
		
		return Out;
	}
	
	void PrepareData(HashType const &ID, ??? LibraryInfo)
	{
		if (!DataStatus.File.is_open() || (ID != DataStatus.ID))
			DataStatus.File.swap({Out.Path, std::fstream::in});
		DataStatus.File.seekg(From * ChunkSize);
		DataStatus.ID = ID;
		DataStatus.Chunk = From;
	}
	
	void Review(void)
	{
		if (Request.File.is_open() && ((Clock::now() - Request.LastResponse) > std::chrono::time(10)))
			Send(NP1V1Request, Connection.Request.ID, Connection.Request.Pieces.Next());
	}
};

Core::Core(bool Listen, std::string const &Host, uint8_t Port) :
	TempPath{bfs::temp_directory_path() / bfs::unique_path()},
	ID{std::uniform_int_distribution<uint64_t>{}(std::mt19937{})},
	Net<CoreConnection>
	{
		[](std::string const &Host, uint16_t Port, int Socket) // Create connection
		{ 
			auto Out = new CoreConnection{Host, Port, Socket};
			for (auto Item : Library.Media())
				Out->Announce.emplace(Item->ID, Item->Size);
			return Out;
		},
		[this](CoreConnection &Info) // Idle write
		{
			if (!Announce.empty())
			{
				Info.Send(NP1V1Prepare, Announce.front().ID, Announce.front().Size);
				Announce.pop();
				return true;
			}
			
			if (DataStatus.File)
			{
				std::vector<uint8_t> Data(ChunkSize);
				DataStatus.File.read(&Data[0], Data.size());
				Info.Send(NP1V1Data, DataStatus.ID, Chunk++, Data);
				return true;
			}
		},
		[this](CoreConnection &State, uint64_t const &InstanceID, uint64_t const &SystemTime) // Clock
		{
			Net.Forward(NP1V1Clock, State, InstanceID, SystemTime);
			if (ClockCallback) ClockCallback(InstanceID, SystemTime);
		},
		[this](CoreConnection &State, HashType const &MediaID, uint64_t const &Size) // Prepare
		{
			bool Found = Library.Get(MediaID, Size);
			if (Found) return;
			PendingRequests.emplace_back(MediaID, Size);
		},
		[this](CoreConnection &State, HashType const &MediaID, uint64_t const &From) // Request
		{
			auto Out = Library.Get(MediaID);
			if (!Out) return;
			Net.WakeIdleWrite();
		},
		[this](CoreConnection &State, HashType const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes) // Data
		{
			auto Finished = Info.Receive(MediaID, Chunk, Bytes);
			if (!Finished) return;
			Library.Add(Finished.ID, Finished.Size, Finished.Path);
		},
		[this](CoreConnection &State, HashType const &MediaID, uint64_t const &MediaTime, uint64_t const &SystemTime) // Play
		{
			Net.Forward(NP1V1Play, State, MediaID, MediaTime, SystemTime);
			State.LastPlaying = true;
			State.LastMediaTime = MediaTime;
			State.LastSystemTime = SystemTime;
			if (PlayCallback) PlayCallback(MediaID, MediaTime, SystemTime);
		},
		[this](CoreConnection &Info) // Stop
		{
			Net.Forward(NP1V1Stop, Info);
			LastPlaying = false;
			LastMediaTime = MediaTime;
			LastSystemTime = SystemTime;
			if (StopCallback) StopCallback();
		},
		[this](CoreConnection &State, std::string const &Message) // Chat
		{
			Net.Forward(NP1V1Chat, State, Message);
			if (ChatCallback) ChatCallback(Message);
		},
	}
{
	Net.OpenTimer(10, [this](void) // Timer
	{
		auto Now = Clock::now();
		
		Net.Broadcast(NP1V1Clock, ID, SystemTime);
		
		for (auto State : Connections)
			State.Review(Now);
	});
	Net.Open(Listen ? new CustomListener<CoreConnection>(Host, Port) : new CoreConnection(Host, Port));
}

Core::~Core(void)
{
	bfs::remove_all(TempPath);
}
