#include "core.h"

#include <random>

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
			Assert(!Extend || (Got == Extend));
			if (Got && Extend)
				NewRuns.back() += Length;
			else NewRuns.push_back(Length);
		}
		Position += Length;
		Got = !Got;
	}
#ifndef NDEBUG
	size_t OrigSize = 0;
	for (auto Length : Runs) OrigSize += Length;
	size_t NewSize = 0;
	for (auto Length : NewRuns) NewSize += Length;
	Assert(OrigSize, NewSize);
#endif
	Runs.swap(NewRuns);
}

CoreConnection::CoreConnection(Core &Parent, std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop) :
	Network<CoreConnection>::Connection{Host, Port, Socket, EVLoop, *this}, Parent(Parent), SentPlayState{false}
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Established connection to " << Host << ":" << Port);
}

bool CoreConnection::IdleWrite(void)
{
	if (!SentPlayState)
	{
		// There could be a race condition with user play or incoming plays being double sent, but it shouldn't affect much
		if (Parent.Last.Playing)
			Send(NP1V1Play{}, Parent.Last.MediaID, Parent.Last.MediaTime, Parent.Last.SystemTime);
		SentPlayState = true;
	}

	if (!Announce.empty())
	{
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Announcing " << FormatHash(Announce.front().ID) << " size " << Announce.front().Size);
		Send(NP1V1Prepare{}, Announce.front().ID, Announce.front().Extension, Announce.front().Size);
		Announce.pop();
		return true;
	}

	if (RequestNext()) return true;

	if (Response.File.is_open() && !Response.File.eof())
	{
		std::vector<uint8_t> Data(ChunkSize);
		Assert(Response.File.tellg(), Response.Chunk * ChunkSize);
		Response.File.read((char *)&Data[0], ChunkSize);
		std::streamsize Read = Response.File.gcount();
		if (Read > 0)
		{
			Data.resize(static_cast<size_t>(Read));
			Send(NP1V1Data{}, Response.ID, Response.Chunk, Data);
			if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Sent " << FormatHash(Response.ID) << " chunk " << Response.Chunk << " " << (Response.Chunk * ChunkSize) << " - " << (Response.Chunk * ChunkSize + Data.size() - 1) << " (" << Data.size() << ")");
			Assert((Read == ChunkSize) || (Response.File.eof()));
			++Response.Chunk;
			if (!Response.File.eof()) return true;
		}
	}

	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, "Nothing to idly write, stopping.");
	return false;
}

void CoreConnection::HandleTimer(uint64_t const &Now)
{
	Send(NP1V1Clock{}, Parent.ID, Now - 5000);

	if (Request.File.is_open() && ((GetNow() - Request.LastResponse) > 10 * 1000))
	{
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Re-requesting " << FormatHash(Request.ID) << " from chunk " << Request.Pieces.Next());
		Send(NP1V1Request{}, Request.ID, Request.Pieces.Next());
	}
}

void CoreConnection::Handle(NP1V1Clock, uint64_t const &InstanceID, uint64_t const &SystemTime)
{
	Parent.Net.Forward(NP1V1Clock{}, *this, InstanceID, SystemTime);
	if (Parent.ClockCallback) Parent.ClockCallback(InstanceID, SystemTime);
}

void CoreConnection::Handle(NP1V1Prepare, HashT const &MediaID, std::string const &Extension, uint64_t const &Size)
{
	auto Found = Parent.Library.find(MediaID);
	if (Found != Parent.Library.end()) return;
	if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Preparing " << FormatHash(MediaID) << " size " << Size);
	Parent.Net.Forward(NP1V1Prepare{}, *this, MediaID, Extension, Size);
	PendingRequests.emplace(MediaID, Extension, Size);
	if (Request.Pieces.Finished())
		RequestNext();
}

void CoreConnection::Handle(NP1V1Request, HashT const &MediaID, uint64_t const &From)
{
	auto Out = Parent.Library.find(MediaID);
	if (Out == Parent.Library.end()) return;
	if (!Response.File.is_open() || (MediaID != Response.ID))
	{
		if (Response.File.is_open()) Response.File.close();
		Response.File.open(Out->second.Path, std::fstream::in);
	}
	Response.File.seekg(static_cast<std::streamsize>(From * ChunkSize));
	Response.ID = MediaID;
	Response.Chunk = From;
	WakeIdleWrite();
}

void CoreConnection::Handle(NP1V1Data, HashT const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes)
{
	if (MediaID != Request.ID) return;
	if (Chunk != Request.Pieces.Next()) return;
	Assert(Request.File);
	Assert(Request.File.tellp(), Chunk * ChunkSize);
	if ((Bytes.size() != ChunkSize) && (Chunk * ChunkSize + Bytes.size() != Request.Size)) return; // Probably an error condition
	Request.Pieces.Set(Chunk);
	Request.File.write((char const *)&Bytes[0], Bytes.size());
	Request.LastResponse = GetNow();
	if (Request.Pieces.Finished())
	{
		Request.File.close();
		auto Now = GetNow();
		Parent.PruneLibrary(Now);
		Parent.Library.emplace(Request.ID, Core::LibraryInfo{Request.Size, Request.Path, GetNow()});
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Finished receiving " << FormatHash(Request.ID));
		if (Parent.AddCallback) Parent.AddCallback(Request.ID, Request.Path);

		RequestNext();
	}
}

void CoreConnection::Handle(NP1V1Play, HashT const &MediaID, MediaTimeT const &MediaTime, uint64_t const &SystemTime)
{
	Parent.Net.Forward(NP1V1Play{}, *this, MediaID, MediaTime, SystemTime);
	Parent.Last.Playing = true;
	Parent.Last.MediaID = MediaID;
	Parent.Last.MediaTime = MediaTime;
	Parent.Last.SystemTime = SystemTime;
	if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Received play for " << FormatHash(MediaID) << ":" << MediaTime << " starting at " << SystemTime);
	if (Parent.PlayCallback) Parent.PlayCallback(MediaID, MediaTime, SystemTime);
}

void CoreConnection::Handle(NP1V1Stop)
{
	Parent.Net.Forward(NP1V1Stop{}, *this);
	Parent.Last.Playing = false;
	if (Parent.StopCallback) Parent.StopCallback();
}

void CoreConnection::Handle(NP1V1Chat, std::string const &Message)
{
	Parent.Net.Forward(NP1V1Chat{}, *this, Message);
	if (Parent.ChatCallback) Parent.ChatCallback(Message);
}

bool CoreConnection::RequestNext(void)
{
	while (!PendingRequests.empty())
	{
		{
			auto Found = Parent.Library.find(PendingRequests.front().ID);
			if (Found != Parent.Library.end())
			{
				PendingRequests.pop();
				continue;
			}
		}
		Request.ID = PendingRequests.front().ID;
		Request.Size = PendingRequests.front().Size;
		Request.Pieces = {1 + ((Request.Size - 1) / ChunkSize)};
		Request.Path = Parent.TempPath / (FormatHash(Request.ID) + PendingRequests.front().Extension);
		if (Request.File.is_open()) Request.File.close();
		Request.File.open(Request.Path, std::fstream::out);
		if (!Request.File)
		{
			if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Could not create core library file " << Request.Path);
			PendingRequests.pop();
			continue;
		}
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, String() << "Requesting " << FormatHash(Request.ID) << " from chunk " << Request.Pieces.Next());
		Send(NP1V1Request{}, Request.ID, Request.Pieces.Next());
		PendingRequests.pop();
		return true;
	}
	return false;
}

Core::Core(bool PruneOldItems) :
	TempPath{bfs::temp_directory_path() / bfs::unique_path()},
	ID{GeneratePUID()},
	Prune{PruneOldItems},
	Last{false},
	Net
	{
		std::make_tuple(NP1V1Clock{}, NP1V1Prepare{}, NP1V1Request{}, NP1V1Data{}, NP1V1Play{}, NP1V1Stop{}, NP1V1Chat{}),
		[this](std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop) // Create connection
		{
			auto Out = new CoreConnection{*this, Host, Port, Socket, EVLoop};
			for (auto Item : Library)
				Out->Announce.emplace(Item.first, Item.second.Path.extension().string(), Item.second.Size);
			return Out;
		},
		10.0f
	}
{
	Net.LogCallback = [&](std::string const &Message)
		{ if (LogCallback) LogCallback(Important, "Network: " + Message); };
	bfs::create_directory(TempPath);
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

void Core::Add(HashT const &MediaID, size_t Size, bfs::path const &Path)
{
	try
	{
		auto Now = GetNow();
		PruneLibrary(Now);
		Library.emplace(MediaID, LibraryInfo{Size, Path, Now});

		for (auto &Connection : Net.GetConnections())
		{
			Connection->Announce.emplace(MediaID, Path.extension().string(), Size);
			Connection->WakeIdleWrite();
		}
	}
	catch (...) {} // TODO Log/warn?
}

void Core::Play(HashT const &MediaID, MediaTimeT Position, uint64_t SystemTime)
{
	Net.Broadcast(NP1V1Play{}, MediaID, Position, SystemTime);
	Last.Playing = true;
	Last.MediaID = MediaID;
	Last.MediaTime = Position;
	Last.SystemTime = SystemTime;
}

void Core::Stop(void)
{
	Net.Broadcast(NP1V1Stop{});
	Last.Playing = false;
}

void Core::Chat(std::string const &Message)
	{ Net.Broadcast(NP1V1Chat{}, Message); }

Core::PlayStatus const &Core::GetPlayStatus(void) const
	{ return Last; }

void Core::PruneLibrary(uint64_t const &Now)
{
	if (!Prune) return;
	for (auto Item = Library.begin(); Item != Library.end(); )
	{
		if (Now - Item->second.Created > 1000 * 60 * 60)
		{
			bfs::remove(Item->second.Path);
			Item = Library.erase(Item);
		}
		else ++Item;
	}
}
