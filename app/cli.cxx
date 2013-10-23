#include "clientcore.h"
#include "regex.h"

#include <csignal>
#include <readline/readline.h>
#include <readline/history.h>

// Asynch logging and writing to the screen and stuff
std::mutex CallsMutex;
std::vector<std::function<void(void)>> Calls;

void Async(std::function<void(void)> const &Call)
{
	std::lock_guard<std::mutex> Lock(CallsMutex);
	Calls.push_back(Call);
	raise(SIGALRM);
}

enum class PlaylistColumns
{
	Track,
	Artist,
	Album,
	Title
};

enum struct PlayState { Deselected, Pause, Play };

struct PlaylistType
{
	private:
		struct PlaylistInfo
		{
			HashT Hash;
			PlayState State;
			Optional<uint16_t> Track;
			std::string Title;
			std::string Album;
			std::string Artist;
			PlaylistInfo(HashT const &Hash, decltype(State) const &State, Optional<uint16_t> const &Track, std::string const &Title, std::string const &Album, std::string const &Artist) : Hash(Hash), State{State}, Track{Track}, Title{Title}, Album{Album}, Artist{Artist} {}
			PlaylistInfo(void) {}
		};
		std::vector<PlaylistInfo> Playlist;
		Optional<size_t> Index;
	public:

	Optional<size_t> Find(HashT const &Hash)
	{
		for (size_t Index = 0; Index < Playlist.size(); ++Index) if (Playlist[Index].Hash == Hash) return Index;
		return {};
	}

	void AddUpdate(MediaInfo const &Item)
	{
		auto Found = Find(Item.Hash);
		if (!Found)
		{
			Playlist.emplace_back(Item.Hash, PlayState::Deselected, Item.Track, Item.Title, Item.Album, Item.Artist);
		}
		else
		{
			Playlist[*Found].Track = Item.Track;
			Playlist[*Found].Title = Item.Title;
			Playlist[*Found].Album = Item.Album;
			Playlist[*Found].Artist = Item.Artist;
		}
	}

	void Remove(HashT const &Hash)
	{
		auto Found = Find(Hash);
		if (!Found) return;
		if (Index && (*Found == *Index)) Index = {};
		Playlist.erase(Playlist.begin() + *Found);
	}

	bool Select(HashT const &Hash)
	{
		auto Found = Find(Hash);
		if (!Found) return true;
		if (Index)
		{
			Playlist[*Index].State = PlayState::Deselected;
		}
		bool Out = Index == Found;
		Index = *Found;
		Playlist[*Index].State = PlayState::Pause;
		return Out;
	}

	Optional<bool> IsPlaying(void)
	{
		if (!Index) return {};
		return Playlist[*Index].State == PlayState::Play;
	}

	Optional<HashT> GetID(size_t Row) const
	{
		if (Row >= Playlist.size()) return {};
		return Playlist[Row].Hash;
	}

	Optional<HashT> GetCurrentID(void) const
	{
		if (!Index) return {};
		return Playlist[*Index].Hash;
	}

	Optional<PlaylistInfo> GetCurrent(void) const
	{
		if (!Index) return {};
		return Playlist[*Index];
	}

	std::vector<PlaylistInfo> const &GetItems(void) const { return Playlist; }

	Optional<HashT> GetNextID(void) const
	{
		if (!Index)
		{
			if (Playlist.empty()) return {};
			else return Playlist.front().Hash;
		}
		else
		{
			if (*Index + 1 >= Playlist.size())
				return Playlist.front().Hash;
			return Playlist[*Index + 1].Hash;
		}
	}

	Optional<HashT> GetPreviousID(void) const
	{
		if (!Index)
		{
			if (Playlist.empty()) return {};
			else return Playlist.back().Hash;
		}
		else
		{
			if (*Index == 0)
				return Playlist.back().Hash;
			return Playlist[*Index - 1].Hash;
		}
	}

	void Play(void)
	{
		if (!Index) return;
		Playlist[*Index].State = PlayState::Play;
	}

	void Stop(void)
	{
		if (!Index) return;
		Playlist[*Index].State = PlayState::Pause;
	}

	void Shuffle(void)
	{
		HashT CurrentID;
		if (Index)
		{
			assert(*Index < Playlist.size());
			CurrentID = Playlist[*Index].Hash;
		}
		std::random_shuffle(Playlist.begin(), Playlist.end());
		auto Found = Find(CurrentID);
		if (Found) Index = *Found;
	}

	struct SortFactor
	{
		PlaylistColumns Column;
		bool Reverse;
		SortFactor(PlaylistColumns const Column, bool const Reverse) : Column{Column}, Reverse{Reverse} {}
	};
	void Sort(std::list<SortFactor> const &Factors)
	{
		HashT CurrentID;
		if (Index)
		{
			assert(*Index < Playlist.size());
			CurrentID = Playlist[*Index].Hash;
		}
		std::stable_sort(Playlist.begin(), Playlist.end(), [&Factors](PlaylistInfo const &First, PlaylistInfo const &Second)
		{
			for (auto &Factor : Factors)
			{
				auto const Fix = [&Factor](bool const Verdict) { if (Factor.Reverse) return !Verdict; return Verdict; };
				switch (Factor.Column)
				{
					case PlaylistColumns::Track:
						if (First.Track == Second.Track) continue;
						if (!First.Track) return Fix(true);
						if (!Second.Track) return Fix(false);
						return Fix(*First.Track < *Second.Track);
					case PlaylistColumns::Title:
						if (First.Title == Second.Title) continue;
						return Fix(First.Title < Second.Title);
					case PlaylistColumns::Album:
						if (First.Album == Second.Album) continue;
						return Fix(First.Album < Second.Album);
					case PlaylistColumns::Artist:
						if (First.Artist == Second.Artist) continue;
						return Fix(First.Artist < Second.Artist);
					default: assert(false); continue;
				}
			}
			return false;
		});
		auto Found = Find(CurrentID);
		if (Found) Index = *Found;
	}
};

int main(int argc, char **argv)
{
	{
		struct sigaction SignalAction{};
		//SignalAction.sa_handler = SIG_IGN;
		SignalAction.sa_handler = [](int) {};
		sigaction(SIGALRM, &SignalAction, nullptr);
	}
	
	std::string Handle{"Dog"};
	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (argc >= 2) Handle = argv[1];
	if (argc >= 3) Host = argv[2];
	if (argc >= 4) String(argv[3]) >> Port;

	// Play state and stuff
	bool Alive = true;
	struct
	{
		void Request(void) { Count = 0u; }
		void Ack(void) { Count = std::min(2u, Count + 1u); }
		bool InControl(void) { return Count == 1u; }
		void Maintain(void) { if (InControl()) Request(); }
		unsigned int Count = 2u;
	} Volition;
	PlaylistType Playlist;

	ClientCore Core{Handle, 0.75f};
	Core.LogCallback = [](std::string const &Message) { Async([=](void) { std::cout << Message << "\n"; }); };
	Core.SeekCallback = [&](float Time) { Async([=](void)
	{
		unsigned int Minutes = Time / 1000 / 60;
		std::cout << "Time: " << Minutes << ":" << (Time / 1000 - (Minutes * 60)) << "\n";
	}); };
	Core.AddCallback = [&](MediaInfo Item) { Async([&](void) { Playlist.AddUpdate(Item); }); };
	Core.UpdateCallback = [&](MediaInfo Item) { Async([&](void) { Playlist.AddUpdate(Item); }); };
	Core.SelectCallback = [&](HashT const &MediaID) { Async([&](void)
	{
		Volition.Ack();
		bool WasSame = Playlist.Select(MediaID);
		auto Playing = Playlist.GetCurrent();
		Assert(Playing);
		if (!WasSame)
			std::cout << "Playing " << Playing->Title << "\n";
	}); };
	Core.PlayCallback = [&](void) { Async([&](void) { Playlist.Play(); }); };
	Core.StopCallback = [&](void) { Async([&](void) { Playlist.Stop(); }); };
	Core.EndCallback = [&](void)
	{
		Async([&](void)
		{
			if (!Volition.InControl()) return;
			auto NextID = Playlist.GetNextID();
			if (!NextID) return;
			Volition.Request();
			Core.Play(*NextID, 0ul);
		});
	};

	std::cout << "Connecting to " << Host << ":" << Port << std::endl;
	Core.Open(false, Host, Port);

	// Define commands
	auto AddCommand = [&](std::string const &Line)
	{
		std::cout << "Add: " << Line << "\n";
	};
	auto ListCommand = [&](std::string const &Line)
	{
		size_t Index = 0;
		for (auto const &Item : Playlist.GetItems())
			std::cout <<
				((Item.State == PlayState::Play) ? "> " :
					((Item.State == PlayState::Pause) ? "= " :
						"  ")) <<
				(Index++) << ". " <<
				Item.Title <<
				"\n";
	};
	auto SortCommand = [&](std::string const &Line)
	{
		std::cout << "Sort: " << Line << "\n";
	};
	auto ShuffleCommand = [&](std::string const &Line)
	{
		Playlist.Shuffle();
		ListCommand({});
	};
	auto SelectCommand = [&](std::string const &Line)
	{
		size_t Index = 0;
		static Regex::Parser<size_t> Parse("\\s*(\\d+)$");
		if (Parse(Line, Index))
		{
			auto SelectID = Playlist.GetID(Index);
			if (!SelectID) return;
			Volition.Request();
			Core.Play(*SelectID, 0ul);
		}
		else std::cout << "No index specified." << "\n";
	};
	auto NextCommand = [&](std::string const &Line)
	{
		auto NextID = Playlist.GetNextID();
		if (!NextID) return;
		Volition.Request();
		Core.Play(*NextID, 0ul);
	};
	auto BackCommand = [&](std::string const &Line)
	{
		auto PreviousID = Playlist.GetPreviousID();
		if (!PreviousID) return;
		Volition.Request();
		Core.Play(*PreviousID, 0ul);
	};
	auto PlayCommand = [&](std::string const &Line)
	{
		uint64_t Minutes = 0;
		uint64_t Seconds = 0;
		static Regex::Parser<uint64_t, uint64_t> Parse("\\s*(\\d+):(\\d+)$");
		if (Parse(Line, Minutes, Seconds))
		{
			auto CurrentID = Playlist.GetCurrentID();
			if (CurrentID)
			{
				Volition.Maintain();
				Core.Play(*CurrentID, MediaTimeT((Minutes * 60 + Seconds) * 1000));
			}
		}
		else Core.Play();
	};
	auto StopCommand = [&](std::string const &Line) { Core.Stop(); };
	auto QuitCommand = [&](std::string const &Line) { Alive = false; };
	auto NowCommand = [&](std::string const &Line) { Core.GetTime(); };

	std::map<std::string, std::function<void(std::string const &Line)>> Commands
	{
		{"add", AddCommand},
		{"list", ListCommand},
		{"ls", ListCommand},
		{"sort", SortCommand},
		{"shuffle", ShuffleCommand},
		{"select", SelectCommand},
		{"next", NextCommand},
		{"back", BackCommand},
		{"previous", BackCommand},
		{"play", PlayCommand},
		{"stop", StopCommand},
		{"pause", StopCommand},
		{"quit", QuitCommand},
		{"exit", QuitCommand},
		{"now", NowCommand}
	};
	
	std::thread Throd{[&](void) { while (true) { Async([](void) { std::cout << "Hi" << std::endl; }); sleep(5); } }};

	// Readline loop
	rl_getc_function = [](FILE *File) 
	{ 
		int Out = 0;
		auto Result = read(fileno(File), &Out, 1);
		std::cout << "=" << Result << ", " << (int)Out << "\n"; 
		if (Result == -1 && errno == EINTR) std::cout << "EINTR" << std::endl;
		if (Result != 1) Out = EOF;
		return Out;
	};
	while (Alive)
	{
		{
			std::lock_guard<std::mutex> Lock(CallsMutex);
			for (auto const &Call : Calls) Call();
			Calls.clear();
		}
		
		std::cout << "a" << std::endl;
		std::unique_ptr<char[], void (*)(void *)> Line{readline("This is my prompt: "), &std::free};
		std::cout << "b" << std::endl;
		
		if (!Line) continue;

		if (Line[0] == 0) continue;

		std::cout << "Line:" << Line.get() << std::endl;

		if (Line[0] != ' ')
		{
			std::cout << "c" << std::endl;
			Core.Chat(Line.get());
			std::cout << "d" << std::endl;
			continue;
		}

		add_history(Line.get());

		auto Found = Commands.lower_bound(&Line[1]);

		if ((Found == Commands.end()) ||
			(Line[1] != Found->first[0]))
		{
			std::cout << "Unknown command.\n";
			continue;
		}

		size_t Start = 1;
		for (; (Line[Start] != 0) && (Line[Start] != ' '); ++Start) {};
		Found->second(&Line[Start]);
		std::cout << "e" << std::endl;
	}

	return 0;
}
