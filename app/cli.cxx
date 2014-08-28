#include "clientcore.h"
#include "regex.h"

#include <csignal>
#include <readline/readline.h>
#include <readline/history.h>
#include <glob.h>
#include <iomanip>

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

std::string FormatItem(PlaylistType::PlaylistInfo const &Item, OptionalT<size_t> Index, bool State, bool Album, bool TrackNumber, bool Artist)
{
	StringT Out;
	if (State) Out << ((Item.State == PlayState::Play) ? "> " :
		((Item.State == PlayState::Pause) ? "= " :
		"  "));
	if (Index) Out << *Index << ". ";
	if (Album) Out << Item.Album;
	if (TrackNumber && Item.Track)
	{
		if (Album) Out << "/";
		Out << *Item.Track;
	}
	if (Album || TrackNumber) Out << " - ";
	if (Artist) Out << Item.Artist << " - ";
	Out << Item.Title;
	return Out;
}

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

// Because readline >8=(           )
std::string Handle{"Dog: "};

int main(int argc, char **argv)
{
	{
		struct sigaction SignalAction{};
		SignalAction.sa_handler = [](int) {};
		sigaction(SIGALRM, &SignalAction, nullptr);
		sigaction(SIGINT, &SignalAction, nullptr);
	}

	InitializeTranslation("raoliocli");

	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (argc >= 2)
	{
		Handle = argv[1];
		if ((Handle == "--help") || (Handle == "-h"))
		{
			std::cout << "raoliocli [HANDLE] [HOST] [PORT]" << std::endl;
			return 0;
		}
		Handle += ": ";
	}
	if (argc >= 3) Host = argv[2];
	if (argc >= 4) StringT(argv[3]) >> Port;

	// Play state and stuff
	struct
	{
		void Request(void) { Count = 0u; }
		void Ack(void) { Count = std::min(2u, Count + 1u); }
		bool InControl(void) { return Count == 1u; }
		void Maintain(void) { if (InControl()) Request(); }
		unsigned int Count = 2u;
	} Volition;
	struct CLIPlaylistType : PlaylistType
	{
		void Reverse(void) { std::reverse(Playlist.begin(), Playlist.end()); }
		void Sink(HashT const &Hash)
		{
			auto Found = Playlist.begin();
			for (; Found != Playlist.end(); ++Found)
				if (Found->Hash == Hash) break;
			if (Found == Playlist.end()) return;
			auto Copy = *Found;
			Playlist.erase(Found);
			Playlist.push_back(std::move(Copy));
		}
	} Playlist;

	uint64_t Volume = 75;
	ClientCore Core{(float)Volume / 100.0f};
	Core.LogCallback = [](std::string const &Message) { Async([=](void) { std::cout << Message << "\n"; }); };
	Core.SeekCallback = [&](float Percent, float Duration) { Async([=](void)
	{
		auto Time = Duration * Percent;
		unsigned int Minutes = Time / 60;
		std::cout << Local("Time: ^0:^1", Minutes, StringT() << std::setfill('0') << std::setw(2) << ((unsigned int)Time - (Minutes * 60))) << "\n";
	}); };
	Core.AddCallback = [&](MediaInfo Item) { Async([&, Item](void) { Playlist.AddUpdate(Item); }); };
	Core.RemoveCallback = [&](HashT const &MediaID) { Async([&, MediaID](void) { Playlist.Remove(MediaID); }); };
	Core.UpdateCallback = [&](MediaInfo Item) { Async([&, Item](void) { Playlist.AddUpdate(Item); }); };
	Core.SelectCallback = [&](HashT const &MediaID) { Async([&, MediaID](void)
	{
		Volition.Ack();
		bool WasSame = Playlist.Select(MediaID);
		auto Playing = Playlist.GetCurrent();
		Assert(Playing);
		if (!WasSame)
			std::cout << Local("Playing ^0", Playing->Title) << "\n";
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

	std::cout << Local("Connecting to ^0:^1", Host, Port) << std::endl;
	Core.Open(false, Host, Port);

	// Define commands
	std::cout << Local("Type -help for a list of commands.") << std::endl;
	struct CommandT
	{
		CommandT(void) {}
		CommandT(std::string const &Description, std::function<void(std::string const &Line)> const &Function) : Description(Description), Function(Function) {}
		CommandT(std::function<void(std::string const &Line)> const &Function) : Function(Function) {}
		std::string Description;
		std::function<void(std::string const &Line)> Function;
	};
	std::map<std::string, CommandT> Commands;
	Commands["add"] =
	{
		Local("-add PATTERN...\tAdds all filenames matching any PATTERN to playlist.") + "\n",
		[&](std::string const &Line)
		{
			StringSplitter Splitter{{' '}, true};
			Splitter.Process(Line);
			for (auto Pattern : Splitter.Results())
			{
				glob_t Globbed;
				int Result = glob(Pattern.c_str(), GLOB_TILDE, nullptr, &Globbed);
				if (Result != 0)
				{
					std::cerr << Local("Invalid file '^0'", Pattern) << std::endl;
					continue;
				}

				for (decltype(Globbed.gl_pathc) Index = 0; Index < Globbed.gl_pathc; ++Index)
				{
					bfs::path Filename = Globbed.gl_pathv[Index];
					if (bfs::is_directory(Filename)) continue;
					auto Hash = HashFile(Filename);
					assert(Hash);
					if (!Hash) break;
					Core.Add(Hash->first, Hash->second, Filename);
				}
			}
		}
	};
	Commands["remove"] =
	{
		Local("-remove -a|INDEX...\tRemoves INDEX or all items from playlist.") + "\n",
		[&](std::string const &Line)
		{
			std::list<HashT> Hashes;
			StringSplitter Splitter{{' '}, true};
			Splitter.Process(Line);
			for (auto Column : Splitter.Results())
			{
				if ((Column == "-a") || (Column == "--all"))
				{
					Core.RemoveAll();
					return;
				}

				size_t Index = 0;
				static Regex::ParserT<size_t> Parse("\\s*(\\d+)$");
				if (Parse(Column, Index))
				{
					auto SelectID = Playlist.GetID(Index);
					if (!SelectID)
					{
						std::cout << Local("Invalid index: ^0", Column) << "\n";
						return;
					}
					Hashes.push_back(*SelectID);
				}
				else
				{
					std::cout << Local("Invalid index specified.") << "\n";
					return;
				}
			}
			for (auto Remove : Hashes)
				Core.Remove(Remove);
		}
	};
	Commands["rm"] = Commands["remove"];
	Commands["list"] =
	{
		"-list [-a][-all] [artist] [album] [track]\n"
		"\t" + Local("Lists all media in playlist, displaying columns as specified.") + "\n",
		[&](std::string const &Line)
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
				std::cout << FormatItem(Item, Index++, true, Artist, Album, TrackNumber) << "\n";
		}
	};
	Commands["ls"] = Commands["list"];
	Commands["sort"] =
	{
		"-sort [-][artist|album|track|title]...\n"
		"\t" + Local("Sorts playlist by the specified columns, with increasing specifity. - reverses column order.") + "\n",
		[&](std::string const &Line)
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
			Commands["list"].Function("-a");
		}
	};
	Commands["shuffle"] =
	{
		[&](std::string const &Line)
		{
			Playlist.Shuffle();
			Commands["list"].Function("-a");
		}
	};
	Commands["reverse"] =
	{
		"-reverse\t" + Local("Reverses the playlist.") + "\n",
		[&](std::string const &Line)
		{
			Playlist.Reverse();
			Commands["list"].Function("-a");
		}
	};
	Commands["sink"] =
	{
		"-sink INDEX\t" + Local("Moves INDEX to the bottom of the playlist.") + "\n",
		[&](std::string const &Line)
		{
			size_t Index = 0;
			static Regex::ParserT<size_t> Parse("\\s*(\\d+)$");
			if (Parse(Line, Index))
			{
				auto SelectID = Playlist.GetID(Index);
				if (!SelectID) return;
				Playlist.Sink(*SelectID);
				Commands["list"].Function("-a");
			}
			else std::cout << Local("No index specified.") << "\n";
		}
	};
	Commands["select"] =
	{
		"-select INDEX\t" + Local("Plays media at playlist INDEX.  Use list or ls to see indices.") + "\n",
		[&](std::string const &Line)
		{
			size_t Index = 0;
			static Regex::ParserT<size_t> Parse("\\s*(\\d+)$");
			if (Parse(Line, Index))
			{
				auto SelectID = Playlist.GetID(Index);
				if (!SelectID) return;
				Volition.Request();
				Core.Play(*SelectID, 0ul);
			}
			else std::cout << Local("No index specified.") << "\n";
		}
	};
	Commands["next"] =
	{
		[&](std::string const &Line)
		{
			auto NextID = Playlist.GetNextID();
			if (!NextID) return;
			Volition.Request();
			Core.Play(*NextID, 0ul);
		}
	};
	Commands["forward"] = Commands["next"];
	Commands["back"] =
	{
		[&](std::string const &Line)
		{
			auto PreviousID = Playlist.GetPreviousID();
			if (!PreviousID) return;
			Volition.Request();
			Core.Play(*PreviousID, 0ul);
		}
	};
	Commands["previous"] = Commands["back"];
	Commands["play"] =
	{
		"-play [TIME]\t" + Local("Plays the current media if paused.  If TIME is specified (as MM:SS), also seeks the media.") + "\n",
		[&](std::string const &Line)
		{
			uint64_t Minutes = 0;
			uint64_t Seconds = 0;
			static Regex::ParserT<uint64_t, uint64_t> Parse("\\s*(\\d+):(\\d+)$");
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
		}
	};
	Commands["volume"] =
	{
		"-volume VALUE\t" + Local("Sets volume to VALUE.  Value must be in the range [0,100].") + "\n",
		[&](std::string const &Line)
		{
			if (Line.empty())
			{
				std::cout << Local("Volume is ^0", Volume) << "\n";
			}
			else
			{
				static Regex::ParserT<uint64_t> Parse("\\s*(\\d+)$");
				if (Parse(Line, Volume)) Core.SetVolume((float)Volume / 100.0f);
				else std::cout << Local("Bad volume.") << "\n";
			}
		}
	};
	Commands["stop"] = { [&](std::string const &Line) { Core.Stop(); } };
	Commands["pause"] = Commands["stop"];
	Commands["quit"] = { [&](std::string const &Line) { Alive = false; } };
	Commands["exit"] = Commands["quit"];
	Commands["now"] =
	{
		[&](std::string const &Line)
		{
			auto Current = Playlist.GetCurrent();
			if (!Current)
			{
				std::cout << Local("No song selected.") << "\n";
				return;
			}
			std::cout << Local("Playing: ^0", FormatItem(*Current, {}, false, !Current->Album.empty(), Current->Track, !Current->Artist.empty())) << "\n";
			Core.GetTime();
		}
	};
	Commands["help"] =
	{
		"-help\t" + Local("List all commands.") + "\n"
		"-help COMMAND\t" + Local("List help for COMMAND if available.") + "\n",
		[&](std::string const &Line)
		{
			std::string Topic;
			StringT(Line) >> Topic;
			if (Topic.empty())
			{
				size_t Count = 0;
				for (auto const &Command : Commands)
				{
					std::cout << "-" << Command.first << "\t";
					if (Count++ % 6 == 5) std::cout << "\n";
				}
				std::cout << std::endl;
				return;
			}
			auto Found = Commands.find(Topic);
			if (Found == Commands.end()) { std::cout << Local("Help topic unknown.") << "\n"; return; }
			if (Found->second.Description.empty()) { std::cout << Local("No help available.") << "\n"; return; }
			std::cout << Found->second.Description;
		}
	};
	Commands["?"] = Commands["help"];
	Commands["cd"] =
	{
		"-cd\t" + Local("Show working directory.") + "\n"
		"-cd PATH\t" + Local("Change the working directory to PATH.") + "\n",
		[&](std::string const &Line)
		{
			if (Line.empty())
			{
				try { std::cout << bfs::current_path() << "\n"; }
				catch (...) { std::cout << Local("Unable to determine current path.") + "\n"; }
				return;
			}
			std::string Trimmed;
			StringT(Line) >> Trimmed;
			glob_t Globbed;
			int Result = glob(Trimmed.c_str(), GLOB_TILDE, nullptr, &Globbed);
			if (Result != 0)
			{
				std::cout << Local("Invalid file '^0'.", Trimmed) + "\n";
				return;
			}
			if (Globbed.gl_pathc > 1)
			{
				std::cout << Local("Can't cd to multiple directories.  Found ^0 matching.", Globbed.gl_pathc) + "\n";
				return;
			}
			if (chdir(Globbed.gl_pathv[0]) != 0)
			{
				std::cout << Local("Error changing directories.") << "\n";
				return;
			}
		}
	};
	Commands["cwd"] = Commands["pwd"] = Commands["cd"];
	Commands["shell"] =
	{
		"-shell|! COMMAND\t" + Local("Execute COMMAND with system().") + "\n",
		[&](std::string const &Line)
		{
			system(Line.c_str());
		}
	};
	Commands["!"] = Commands["shell"];
	Commands["handle"] =
	{
		[&](std::string const &Line)
		{
			std::string Trimmed;
			StringT(Line) >> Trimmed;
			if (Trimmed.empty()) std::cout << Handle;
			else
			{
				Handle = Trimmed;
				std::cout << Local("New handle is '^0'", Handle) + "\n";
				Handle += ": ";
			}
		}
	};
	Commands["nick"] = Commands["handle"];

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

			rl_set_prompt(Handle.c_str());
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

		if (Line[0] != '-')
		{
			Core.Chat(Handle + Line.get());
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
			std::cout << Local("Unknown command.") + "\n";
			continue;
		}
		assert(Found != Commands.end());

		Found->second.Function(&Line[CutIndex]);
	}

	return 0;
}
