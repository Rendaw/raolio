#include "core.h"

uint64_t GeneratePUID(void) // Probably Unique ID
{
	std::mt19937 Random;
	return std::uniform_int_distribution<uint64_t>{}(Random);
}

FilePieces::FilePieces(void) : Runs{0} {}

FilePieces::FilePieces(uint64_t Size) : Runs{0, Size} {}

bool FilePieces::Finished(void) const { return Runs.size() == 1; }

uint64_t FilePieces::Next(void) const { return Runs[0]; }

bool FilePieces::Get(uint64_t Index)
{
	bool Got = true;
	uint64_t Position = 0;
	for (auto const Length : Runs)
	{
		Position += Length;
		if (Index < Position) return Got;
		Got = !Got;
	}
	return false;
}

void FilePieces::Set(uint64_t Index)
{
	std::vector<uint64_t> NewRuns;
	NewRuns.reserve(Runs.size() + 2);
	bool Got = true;
	bool Extend = false;
	uint64_t Position = 0;
	for (auto const Length : Runs)
	{
		if (!Got && (Index >= Position) && (Index < Position + Length))
		{
			if (Index == Position)
			{
				NewRuns.back() += 1;
				if (Length == 1) Extend = true;
				else NewRuns.push_back(Length - 1);
			}
			else if (Index == Position + Length - 1)
			{
				NewRuns.push_back(Length - 1);
				NewRuns.push_back(1);
				Extend = true;
			}
			else
			{
				NewRuns.push_back(Index - Position);
				NewRuns.push_back(1);
				NewRuns.push_back(Length - (Index - Position) - 1);
			}
		}
		else
		{
			assert(!Extend || (Got == Extend));
			if (Got && Extend)
				NewRuns.back() += Length;
			else NewRuns.push_back(Length);
		}
		Position += Length;
		Got = !Got;
	}
	Runs.swap(NewRuns);
}

CoreConnection::CoreConnection(Core &Parent, std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop) :
	Network<CoreConnection>::Connection{Host, Port, Socket, EVLoop, this}, Parent(Parent) { }

bool CoreConnection::IdleWrite(void)
{
	if (!Announce.empty())
	{
		Send(NP1V1Prepare{}, Announce.front().ID, Announce.front().Size);
		Announce.pop();
		return true;
	}

	if (Response.File.is_open() && !Response.File.eof())
	{
		std::vector<uint8_t> Data(ChunkSize);
		Response.File.read((char *)&Data[0], Data.size());
		Send(NP1V1Data{}, Response.ID, Response.Chunk++, Data);
		if (!Response.File.eof()) return true;
	}

	return false;
}

void CoreConnection::HandleTimer(uint64_t const &Now)
{
	Send(NP1V1Clock{}, Parent.ID, Now);

	if (Request.File.is_open() && ((GetNow() - Request.LastResponse) > 10 * 1000))
		Send(NP1V1Request{}, Request.ID, Request.Pieces.Next());
}

void CoreConnection::Handle(NP1V1Clock, uint64_t const &InstanceID, uint64_t const &SystemTime)
{
	Parent.Net.Forward(NP1V1Clock{}, *this, InstanceID, SystemTime);
	if (Parent.ClockCallback) Parent.ClockCallback(InstanceID, SystemTime);
}

void CoreConnection::Handle(NP1V1Prepare, HashType const &MediaID, uint64_t const &Size)
{
	auto Found = Parent.Library.find(MediaID);
	if (Found != Parent.Library.end()) return;
	Parent.Net.Forward(NP1V1Prepare{}, *this, MediaID, Size);
	PendingRequests.emplace_back(MediaID, Size);
}

void CoreConnection::Handle(NP1V1Request, HashType const &MediaID, uint64_t const &From)
{
	auto Out = Parent.Library.find(MediaID);
	if (Out == Parent.Library.end()) return;
	if (!Response.File.is_open() || (MediaID != Response.ID))
		Response.File.open(Out->second.Path, std::fstream::in);
	Response.File.seekg(From * ChunkSize);
	Response.ID = MediaID;
	Response.Chunk = From;
	WakeIdleWrite();
}

void CoreConnection::Handle(NP1V1Data, HashType const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes)
{
	if (MediaID != Request.ID) return;
	if (Chunk != Request.Pieces.Next()) return;
	assert(Request.File);
	assert(Request.File.tellp() == Chunk * ChunkSize);
	if (Bytes.size() < ChunkSize || (Chunk * ChunkSize + Bytes.size() != Request.Size)) return; // Probably an error condition
	Request.Pieces.Set(Chunk);
	Request.File.write((char const *)&Bytes[0], ChunkSize);
	Request.LastResponse = GetNow();
	if (!Request.Pieces.Finished()) return;

	Request.File.close();
	Parent.Library.emplace(Request.ID, Core::LibraryInfo{Request.Size, Request.Path});
	if (Parent.AddCallback) Parent.AddCallback(Request.ID, Request.Path);

	if (!PendingRequests.empty())
	{
		Request.ID = PendingRequests.front().ID;
		Request.Size = PendingRequests.front().Size;
		Request.Pieces = {};
		Request.Path = Parent.TempPath / FormatHash(Request.ID);
		Request.File.open(Request.Path, std::fstream::out);
		Send(NP1V1Request{}, Request.ID, Request.Pieces.Next());
		PendingRequests.pop();
	}
}

void CoreConnection::Handle(NP1V1Play, HashType const &MediaID, uint64_t const &MediaTime, uint64_t const &SystemTime)
{
	Parent.Net.Forward(NP1V1Play{}, *this, MediaID, MediaTime, SystemTime);
	Parent.LastPlaying = true;
	Parent.LastMediaTime = MediaTime;
	Parent.LastSystemTime = SystemTime;
	if (Parent.PlayCallback) Parent.PlayCallback(MediaID, MediaTime, SystemTime);
}

void CoreConnection::Handle(NP1V1Stop)
{
	Parent.Net.Forward(NP1V1Stop{}, *this);
	Parent.LastPlaying = false;
	if (Parent.StopCallback) Parent.StopCallback();
}

void CoreConnection::Handle(NP1V1Chat, std::string const &Message)
{
	Parent.Net.Forward(NP1V1Chat{}, *this, Message);
	if (Parent.ChatCallback) Parent.ChatCallback(Message);
}

Core::Core(void) :
	TempPath{bfs::temp_directory_path() / bfs::unique_path()},
	ID{GeneratePUID()},
	Net
	{
		std::make_tuple(NP1V1Clock{}, NP1V1Prepare{}, NP1V1Request{}, NP1V1Data{}, NP1V1Play{}, NP1V1Stop{}, NP1V1Chat{}),
		[this](std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop) // Create connection
		{
			auto Out = new CoreConnection{*this, Host, Port, Socket, EVLoop};
			for (auto Item : Library)
				Out->Announce.emplace(Item.first, Item.second.Size);
			return Out;
		},
		10.0f
	}
{
}

Core::~Core(void)
{
	bfs::remove_all(TempPath);
}

void Core::Open(bool Listen, std::string const &Host, uint16_t Port)
{
	Net.Open(Listen, Host, Port);
}

void Core::Transfer(std::function<void(void)> const &Call)
{
	Net.Transfer(Call);
}

void Core::Schedule(float Seconds, std::function<void(void)> const &Call)
{
	Net.Schedule(Seconds, Call);
}

void Core::Add(HashType const &MediaID, bfs::path const &Path)
{
	try
	{
		size_t Size = bfs::file_size(Path);
		Library.emplace(MediaID, LibraryInfo{Size, Path});

		for (auto &Connection : Net.GetConnections())
		{
			Connection->Announce.emplace(MediaID, Size);
			Connection->WakeIdleWrite();
		}
	}
	catch (...) {} // TODO Log/warn?
}

void Core::Play(HashType const &MediaID, uint64_t Position, uint64_t SystemTime)
{
	Net.Broadcast(NP1V1Play{}, MediaID, Position, SystemTime);
}

void Core::Stop(void)
{
	Net.Broadcast(NP1V1Stop{});
}
