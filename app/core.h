#ifndef core_h
#define core_h

#include "protocol.h"
#include "network.h"

DefineProtocol(NetProto1)

// V1
extern constexpr size_t ChunkSize = 512;

DefineProtocolVersion(NetProto1, NP1V1)

DefineProtocolMessage(NP1V1, NP1V1Clock, void(uint64_t InstanceID, uint64_t SystemTime))
template <> struct NetworkChannel<NP1V1Clock> { static constexpr uint8_t Index = 0; static constexpr bool Unordered = false; };

DefineProtocolMessage(NP1V1, NP1V1Prepare, void(HashType MediaID, uint64_t Size))
template <> struct NetworkChannel<NP1V1Prepare> { static constexpr uint8_t Index = 0; static constexpr bool Unordered = false; };

DefineProtocolMessage(NP1V1, NP1V1Request, void(HashType MediaID, uint64_t Chunk))
template <> struct NetworkChannel<NP1V1Request> { static constexpr uint8_t Index = 0; static constexpr bool Unordered = false; };

DefineProtocolMessage(NP1V1, NP1V1Data, void(HashType MediaID, uint64_t Chunk, std::vector<uint8_t> Bytes))
template <> struct NetworkChannel<NP1V1Data> { static constexpr uint8_t Index = 0; static constexpr bool Unordered = false; };

DefineProtocolMessage(NP1V1, NP1V1Play, void(HashType MediaID, uint64_t MediaTime, uint64_t SystemTime))
template <> struct NetworkChannel<NP1V1Play> { static constexpr uint8_t Index = 1; static constexpr bool Unordered = false; };

DefineProtocolMessage(NP1V1, NP1V1Stop, void())
template <> struct NetworkChannel<NP1V1Stop> { static constexpr uint8_t Index = 1; static constexpr bool Unordered = false; };

DefineProtocolMessage(NP1V1, NP1V1Chat, void(std::string Message))
template <> struct NetworkChannel<NP1V1Chat> { static constexpr uint8_t Index = 0; static constexpr bool Unordered = false; };

struct FileCache
{

};

struct FilePieces
{
	FilePieces(uint64_t Size) : Runs{0, Size} {}
	
	bool Get(uint64_t Index)
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
	
	void Set(uint64_t Index)
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
	
	std::vector<uint64_t> Runs;
};

struct Core
{
	Core(bool Listen, std::string const &Host, uint8_t Port);
	~Core(void);
	private:
		std::mutex Mutex;
		bfs::path const TempPath;
		uint64_t const ID;
		FileCache LibraryCache;

		bool LastPlaying = false;
		uint64_t LastMediaTime;
		uint64_t LastSystemTime;

		std::map<HashType, bfs::path> Media;

		Network
		<
			2 * 1024,
			NP1V1Clock, NP1V1Prepare, NP1V1Request, NP1V1Data, NP1V1Play, NP1V1Stop, NP1V1Chat
		> Net;
};

#endif
