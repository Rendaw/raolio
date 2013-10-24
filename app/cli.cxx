#include "clientcore.h"
#include "regex.h"

#include <csignal>
#include <readline/readline.h>
#include <readline/history.h>
#include <glob.h>

class StringSplitter
{
        public:
                StringSplitter(std::set<char> const &Delimiters, bool DropBlanks) :
			Delimiters(Delimiters), DropBlanks(DropBlanks), HotSlash(false), HotQuote(false) {}

                StringSplitter &Process(std::string const &Input)
		{
			std::string Buffer;
			Buffer.reserve(1000);

		#ifdef WINDOWS
			char const Slash = '^';
		#else
			char const Slash = '\\';
		#endif
			char const Quote = '"';
			for (unsigned int CharacterIndex = 0; CharacterIndex < Input.length(); ++CharacterIndex)
			{
				char const &CurrentCharacter = Input[CharacterIndex];
				if (!HotSlash)
				{
					if (CurrentCharacter == Slash)
					{
						HotSlash = true;
						continue;
					}

					if (CurrentCharacter == Quote)
					{
						HotQuote = !HotQuote;
						continue;
					}
				}

				if (HotSlash)
				{
					HotSlash = false;
				}

				if (!HotQuote && (Delimiters.find(CurrentCharacter) != Delimiters.end()))
				{
					if (!DropBlanks || !Buffer.empty())
						Out.push_back(Buffer);
					Buffer.clear();
					continue;
				}

				Buffer.push_back(CurrentCharacter);
			}

			if (!DropBlanks || !Buffer.empty())
				Out.push_back(Buffer);

			return *this;
		}

		bool Finished(void) // Open quotes or unused escape (at end of input)
			{ return !HotSlash && !HotQuote; }

                std::vector<std::string> &Results(void) { return Out; }
	private:
		std::set<char> const Delimiters;
		bool DropBlanks;

		bool HotSlash;
		bool HotQuote;
		std::vector<std::string> Out;
};

// Asynch logging and writing to the screen and stuff
bool Alive = true;
auto MainThread = pthread_self();
std::mutex CallsMutex;
std::vector<std::function<void(void)>> Calls;

void Async(std::function<void(void)> const &Call)
{
	std::lock_guard<std::mutex> Lock(CallsMutex);
	Calls.push_back(Call);
	pthread_kill(MainThread, SIGALRM);
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
		SignalAction.sa_handler = [](int) {};
		sigaction(SIGALRM, &SignalAction, nullptr);
		SignalAction.sa_handler = [](int) { Alive = false; };
		sigaction(SIGINT, &SignalAction, nullptr);
	}

	std::string Handle{"Dog"};
	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (argc >= 2) Handle = argv[1];
	if (argc >= 3) Host = argv[2];
	if (argc >= 4) String(argv[3]) >> Port;

	// Play state and stuff
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
		unsigned int Minutes = Time / 60;
		std::cout << "Time: " << Minutes << ":" << ((unsigned int)Time - (Minutes * 60)) << "\n";
	}); };
	Core.AddCallback = [&](MediaInfo Item) { Async([&, Item](void) { Playlist.AddUpdate(Item); }); };
	Core.UpdateCallback = [&](MediaInfo Item) { Async([&, Item](void) { Playlist.AddUpdate(Item); }); };
	Core.SelectCallback = [&](HashT const &MediaID) { Async([&, MediaID](void)
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
	std::cout << "All commands start with 1 space." << std::endl;
	std::cout << "Type ' help' for a list of commands." << std::endl;
	std::map<std::string, std::function<void(std::string const &Line)>> Commands;
	{
		auto AddCommand = [&](std::string const &Line)
		{
			StringSplitter Splitter{{' '}, true};
			Splitter.Process(Line);
			for (auto Pattern : Splitter.Results())
			{
				glob_t Globbed;
				int Result = glob(Pattern.c_str(), GLOB_TILDE, nullptr, &Globbed);
				if (Result != 0)
				{
					std::cerr << "Invalid file '" << Pattern << "'" << std::endl;
					continue;
				}

				for (decltype(Globbed.gl_pathc) Index = 0; Index < Globbed.gl_pathc; ++Index)
				{
					auto Filename = Globbed.gl_pathv[Index];
					auto Hash = HashFile(Filename);
					assert(Hash);
					if (!Hash) break;
					Core.Add(Hash->first, Hash->second, Filename);
				}
			}
		};
		auto ListCommand = [&](std::string const &Line)
		{
			bool Artist = false;
			bool Album = false;
			bool TrackNumber = false;
			StringSplitter Splitter{{' '}, true};
			Splitter.Process(Line);
			for (auto Column : Splitter.Results())
			{
				if ((Column == "-a") || (Column == "--all"))
				{
					Artist = true;
					Album = true;
					TrackNumber = true;
					break;
				}
				if (Column == "artist") Artist = true;
				else if (Column == "album") Album = true;
				else if (Column == "track") TrackNumber = true;
			}
			size_t Index = 0;
			for (auto const &Item : Playlist.GetItems())
			{
				std::cout <<
					((Item.State == PlayState::Play) ? "> " :
						((Item.State == PlayState::Pause) ? "= " :
							"  ")) <<
					(Index++) << ". ";
				if (Album) std::cout << Item.Album;
				if (TrackNumber && Item.Track)
				{
					if (Album) std::cout << "/";
					std::cout << *Item.Track;
				}
				if (Album || TrackNumber) std::cout << " - ";
				if (Artist) std::cout << Item.Artist << " - ";
				std::cout <<
					Item.Title <<
					"\n";
			}
		};
		auto SortCommand = [&](std::string const &Line)
		{
			std::list<PlaylistType::SortFactor> Factors;
			StringSplitter Splitter{{' '}, true};
			Splitter.Process(Line);
			for (auto Column : Splitter.Results())
			{
				if (Column == "artist") Factors.emplace_back(PlaylistColumns::Artist, false);
				else if (Column == "-artist") Factors.emplace_back(PlaylistColumns::Artist, true);
				else if (Column == "album") Factors.emplace_back(PlaylistColumns::Album, false);
				else if (Column == "-album") Factors.emplace_back(PlaylistColumns::Album, true);
				else if (Column == "track") Factors.emplace_back(PlaylistColumns::Track, false);
				else if (Column == "-track") Factors.emplace_back(PlaylistColumns::Track, true);
				else if (Column == "title") Factors.emplace_back(PlaylistColumns::Title, false);
				else if (Column == "-title") Factors.emplace_back(PlaylistColumns::Title, true);
			}
			Playlist.Sort(Factors);
			ListCommand("-a");
		};
		auto ShuffleCommand = [&](std::string const &Line)
		{
			Playlist.Shuffle();
			ListCommand("-a");
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
		auto VolumeCommand = [&](std::string const &Line)
		{
			uint64_t Volume = 0;
			static Regex::Parser<uint64_t> Parse("\\s*(\\d+)$");
			if (Parse(Line, Volume)) Core.SetVolume((float)Volume / 100.0f);
			else std::cout << "Bad volume.\n";
		};
		auto StopCommand = [&](std::string const &Line) { Core.Stop(); };
		auto QuitCommand = [&](std::string const &Line) { Alive = false; };
		auto NowCommand = [&](std::string const &Line) { Core.GetTime(); };
		auto HelpCommand = [&](std::string const &Line)
		{
			std::string Topic;
			String(Line) >> Topic;
			if (Topic == "help")
			{
				std::cout << "' help'\tList all commands.\n";
				std::cout << "' help COMMAND'\tList help for COMMAND if available.\n";
			}
			else if (Topic == "add")
				std::cout << "' add PATTERN...'\tAdds all filenames matching any PATTERN to playlist.\n";
			else if (Topic == "select")
				std::cout << "' select INDEX'\tPlays media at playlist INDEX.  Use list or ls to see indices.\n";
			else if (Topic == "volume")
				std::cout << "' volume VALUE'\tSets volume to VALUE.  Value must be in the range [0,100].\n";
			else if (Topic == "play")
				std::cout << "' play [TIME]'\tPlays the current media if paused.  If TIME is specified (as MM:SS), also seeks the media.\n";
			else if ((Topic == "list") || (Topic == "ls"))
				std::cout << "' list [-a][-all] [artist] [album] [track]'\n"
					"\tLists all media in playlist, displaying columns as specified.\n";
			else if (Topic == "sort")
				std::cout << "' sort [-][artist|album|track|title]...'\n"
					"\tSorts playlist by the specified columns, with increasing specifity. - reverses column order.\n";
			else if (Topic.empty())
			{
				size_t Count = 0;
				for (auto const &Command : Commands)
				{
					std::cout << "' " << Command.first << "'\t";
					if (Count++ % 6 == 5) std::cout << "\n";
				}
				std::cout << std::endl;
			}
			else std::cout << "No help available.\n";
		};

		Commands["add"] = AddCommand;
		Commands["list"] = ListCommand;
		Commands["ls"] = ListCommand;
		Commands["sort"] = SortCommand;
		Commands["shuffle"] = ShuffleCommand;
		Commands["select"] = SelectCommand;
		Commands["next"] = NextCommand;
		Commands["back"] = BackCommand;
		Commands["previous"] = BackCommand;
		Commands["play"] = PlayCommand;
		Commands["stop"] = StopCommand;
		Commands["pause"] = StopCommand;
		Commands["quit"] = QuitCommand;
		Commands["exit"] = QuitCommand;
		Commands["volume"] = VolumeCommand;
		Commands["now"] = NowCommand;
		Commands["help"] = HelpCommand;
		Commands["?"] = HelpCommand;
	}

	// Readline loop
	rl_getc_function = [](FILE *File)
	{
		int Out = 0;
		int Result = 0;
		do
		{
			char* saved_line;
			int saved_point;
			saved_point = rl_point;
			saved_line = rl_copy_text(0, rl_end);
			rl_set_prompt("");
			rl_replace_line("", 0);
			rl_redisplay();

			{
				std::lock_guard<std::mutex> Lock(CallsMutex);
				for (auto const &Call : Calls) Call();
				Calls.clear();
			}

			rl_set_prompt("");
			rl_replace_line(saved_line, 0);
			rl_point = saved_point;
			rl_redisplay();

			Result = read(fileno(File), &Out, 1);
		} while (Alive && (Result == -1) && (errno == EINTR));
		if (Result != 1) Out = EOF;
		return Out;
	};
	while (Alive)
	{
		std::unique_ptr<char[], void (*)(void *)> Line{readline(""), &std::free};

		if (!Line) continue;

		if (Line[0] == 0) continue;

		if (Line[0] != ' ')
		{
			Core.Chat(Line.get());
			continue;
		}

		add_history(Line.get());

		size_t CutIndex = 1;
		for (; Line[CutIndex] != 0; ++CutIndex)
			if (Line[CutIndex] == ' ') break;
		std::string Command{&Line[1], CutIndex - 1};
		auto Found = Commands.lower_bound(Command);

		if ((Found == Commands.end()) ||
			(Command[0] != Found->first[0]))
		{
			std::cout << "Unknown command.\n";
			continue;
		}
		assert(Found != Commands.end());

		Found->second(&Line[CutIndex]);
	}

	return 0;
}
