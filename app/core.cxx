#include "core.h"

#include "translation/translation.h"

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

CoreConnection::CoreConnection(Core &Parent, std::string const &Host, uint16_t Port, uv_tcp_t *Watcher, std::function<void(CoreConnection &Socket)> const &ReadCallback) :
	Network<CoreConnection>::Connection{Host, Port, Watcher, ReadCallback, *this}, Parent(Parent), SentPlayState{false}
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Established connection to ^0:^1", Host, Port));
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
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Announcing ^0 size ^1", FormatHash(Announce.front().ID), Announce.front().Size));
		Send(NP1V1Prepare{}, Announce.front().ID, Announce.front().Extension, Announce.front().Size, Announce.front().DefaultTitle);
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
			if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Sent ^0 chunk ^1 ^2 - ^3 (^4)", FormatHash(Response.ID), Response.Chunk, Response.Chunk * ChunkSize, Response.Chunk * ChunkSize + Data.size() - 1, Data.size()));
			Assert((Read == ChunkSize) || (Response.File.eof()));
			++Response.Chunk;
			if (!Response.File.eof()) return true;
		}
	}

	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Nothing to idly write, stopping."));
	return false;
}

void CoreConnection::HandleTimer(uint64_t const &Now)
{
	Send(NP1V1Clock{}, Parent.ID, Now);

	if (Request.File.is_open() && ((GetNow() - Request.LastResponse) > 10 * 1000))
	{
		if (Request.Attempts > 10)
			RequestNext();
		else
		{
			if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Re-requesting ^0 from chunk ^1", FormatHash(Request.ID), Request.Pieces.Next()));
			Send(NP1V1Request{}, Request.ID, Request.Pieces.Next());
			++Request.Attempts;
		}
	}

	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Ran timer event."));
}

void CoreConnection::Handle(NP1V1Clock, uint64_t const &InstanceID, uint64_t const &SystemTime)
{
	Parent.Net.Forward(NP1V1Clock{}, *this, InstanceID, SystemTime);
	if (Parent.ClockCallback) Parent.ClockCallback(InstanceID, SystemTime);
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved clock."));
}

void CoreConnection::Handle(NP1V1Prepare, HashT const &MediaID, std::string const &Extension, uint64_t const &Size, std::string const &DefaultTitle)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved prepare."));
	auto Found = Parent.Library.find(MediaID);
	if (Found != Parent.Library.end()) return;
	if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Preparing ^0 size ^1", FormatHash(MediaID), Size));
	Parent.Net.Forward(NP1V1Prepare{}, *this, MediaID, Extension, Size, DefaultTitle);
	PendingRequests.emplace(MediaID, Extension, Size, DefaultTitle);
	if (Request.Pieces.Finished())
		RequestNext();
}

void CoreConnection::Handle(NP1V1Request, HashT const &MediaID, uint64_t const &From)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved request."));
	auto Out = Parent.Library.find(MediaID);
	if (Out == Parent.Library.end()) return;
	if (!Response.File.is_open() || (MediaID != Response.ID))
	{
		if (Response.File.is_open()) Response.File.close();
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("REND Opening '^0'", Out->second.Path.string()));
		Response.File.open(Out->second.Path, std::fstream::in);
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("REND Opened '^0'", Out->second.Path.string()));
		Assert(Response.File.is_open());
		Assert(!Response.File.eof());
		Assert(!Response.File.fail());
		Assert(!Response.File.bad());
	}
	Assert(Response.File.is_open());
	Assert(!Response.File.eof());
	Assert(!Response.File.fail());
	Assert(!Response.File.bad());
	Response.File.seekg(static_cast<std::streamsize>(From * ChunkSize), std::ios::beg);
	Assert(Response.File.tellg(), static_cast<std::streamsize>(From * ChunkSize));
	Response.ID = MediaID;
	Response.Chunk = From;
	WakeIdleWrite();
}

void CoreConnection::Handle(NP1V1Data, HashT const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved ^0 chunk ^1 ^2 - ^3 (^4)", FormatHash(MediaID), Chunk, Chunk * ChunkSize, Chunk * ChunkSize + Bytes.size() - 1, Bytes.size()));
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
		Parent.Library.emplace(Request.ID, Core::LibraryInfo{Request.Size, Request.Path, Request.DefaultTitle});
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Finished receiving ^0", FormatHash(Request.ID)));
		if (Parent.AddCallback) Parent.AddCallback(Request.ID, Request.Path, Request.DefaultTitle);

		RequestNext();
	}
}

void CoreConnection::Handle(NP1V1Remove, HashT const &MediaID)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved remove."));
	Parent.Net.Forward(NP1V1Remove{}, *this, MediaID);
	if (Parent.RemoveCallback) Parent.RemoveCallback(MediaID);
	Parent.RemoveInternal(MediaID);
}

void CoreConnection::Handle(NP1V1Play, HashT const &MediaID, MediaTimeT const &MediaTime, uint64_t const &SystemTime)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved play."));
	Parent.Net.Forward(NP1V1Play{}, *this, MediaID, MediaTime, SystemTime);
	Parent.Last.Playing = true;
	Parent.Last.MediaID = MediaID;
	Parent.Last.MediaTime = MediaTime;
	Parent.Last.SystemTime = SystemTime;
	if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Received play for ^0:^1 starting at ^2", FormatHash(MediaID), MediaTime, SystemTime));
	if (Parent.PlayCallback) Parent.PlayCallback(MediaID, MediaTime, SystemTime);
}

void CoreConnection::Handle(NP1V1Stop)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved stop."));
	Parent.Net.Forward(NP1V1Stop{}, *this);
	Parent.Last.Playing = false;
	if (Parent.StopCallback) Parent.StopCallback();
}

void CoreConnection::Handle(NP1V1Chat, std::string const &Message)
{
	if (Parent.LogCallback) Parent.LogCallback(Core::Useless, Local("Recieved chat."));
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
		Request.Attempts = 0;
		Request.DefaultTitle = PendingRequests.front().DefaultTitle;
		Request.Path = Parent.TempPath / (FormatHash(Request.ID) + PendingRequests.front().Extension);
		if (Request.File.is_open()) Request.File.close();
		Request.File.open(Request.Path, std::fstream::out);
		if (!Request.File)
		{
			if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Could not create core library file ^0", Request.Path));
			PendingRequests.pop();
			continue;
		}
		if (Parent.LogCallback) Parent.LogCallback(Core::Debug, Local("Requesting ^0 from chunk ^1", FormatHash(Request.ID), Request.Pieces.Next()));
		Send(NP1V1Request{}, Request.ID, Request.Pieces.Next());
		PendingRequests.pop();
		return true;
	}
	return false;
}

void CoreConnection::Remove(HashT const &MediaID)
{
	if (Request.ID == MediaID) RequestNext();
	if ((Response.ID == MediaID) && (Response.File.is_open())) Response.File.close();
}

Core::Core(bool PruneOldItems) :
	TempPath{bfs::temp_directory_path() / bfs::unique_path()},
	ID{GeneratePUID()},
	Prune{PruneOldItems},
	Last{false},
	Net
	{
		std::make_tuple(NP1V1Clock{}, NP1V1Prepare{}, NP1V1Request{}, NP1V1Data{}, NP1V1Remove{}, NP1V1Play{}, NP1V1Stop{}, NP1V1Chat{}),
		[this](std::string const &Host, uint16_t Port, uv_tcp_t *Watcher, std::function<void(CoreConnection &Socket)> const &ReadCallback) // Create connection
		{
			auto IdleTime = Net.IdleSince();
			if (Prune && IdleTime && (GetNow() - *IdleTime > 1000 * 60 * 60))
			{
				std::list<HashT> Removing;
				for (auto const &Item : Library)
				{
					auto ItemIterator = Item.second.Path.begin();
					auto TempIterator = TempPath.begin();
					bool IsCached = true;
					while (TempIterator != TempPath.end())
					{
						if ((ItemIterator == Item.second.Path.end()) ||
							(*ItemIterator != *TempIterator))
						{
							IsCached = false;
							break;
						}
						++TempIterator;
						++ItemIterator;
					}
					if (IsCached)
						Removing.push_back(Item.first);
				}
				for (auto const &Hash : Removing)
				{
					if (RemoveCallback) RemoveCallback(Hash);
					Library.erase(Hash);
				}
				bfs::remove_all(TempPath);
				bfs::create_directory(TempPath);
			}
			auto Out = new CoreConnection{*this, Host, Port, Watcher, ReadCallback};
			for (auto Item : Library)
				Out->Announce.emplace(Item.first, Item.second.Path.extension().string(), Item.second.Size, Item.second.DefaultTitle);
			Out->WakeIdleWrite();
			return Out;
		},
		10.0f
	}
{
	Net.LogCallback = [&](std::string const &Message) { if (LogCallback) LogCallback(Important, Local("Network: ^0", Message)); };
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
		Library.emplace(MediaID, LibraryInfo{Size, Path, Path.filename().string()});

		for (auto &Connection : Net.GetConnections())
		{
			Connection->Announce.emplace(MediaID, Path.extension().string(), Size, Path.filename().string());
			Connection->WakeIdleWrite();
		}
	}
	catch (...) {} // TODO Log/warn?
}

void Core::Remove(HashT const &MediaID)
{
	Net.Broadcast(NP1V1Remove{}, MediaID);
	RemoveInternal(MediaID);
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

void Core::RemoveInternal(HashT const &MediaID)
{
	auto Out = Library.find(MediaID);
	if (Out == Library.end()) return;
	Library.erase(Out);
	for (auto &Connection : Net.GetConnections())
		Connection->Remove(MediaID);
}
