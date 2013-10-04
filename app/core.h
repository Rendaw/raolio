#ifndef core_h
#define core_h

#include "protocol.h"
#include "network.h"

extern constexpr uint64_t ChunkSize = 512; // Set for all protocol versions

DefineProtocol(NetProto1)

DefineProtocolVersion(NP1V1, NetProto1)
DefineProtocolMessage(NP1V1Clock, NP1V1, void(uint64_t InstanceID, uint64_t SystemTime))
DefineProtocolMessage(NP1V1Prepare, NP1V1, void(HashType MediaID, uint64_t Size))
DefineProtocolMessage(NP1V1Request, NP1V1, void(HashType MediaID, uint64_t From))
DefineProtocolMessage(NP1V1Data, NP1V1, void(HashType MediaID, uint64_t Chunk, std::vector<uint8_t> Bytes))
DefineProtocolMessage(NP1V1Play, NP1V1, void(HashType MediaID, uint64_t MediaTime, uint64_t SystemTime))
DefineProtocolMessage(NP1V1Stop, NP1V1, void(void))
DefineProtocolMessage(NP1V1Chat, NP1V1, void(std::string Message))

struct FilePieces
{
	FilePieces(uint64_t Size);

	bool Finished(void) const;
	uint64_t Next(void) const;
	bool Get(uint64_t Index);
	void Set(uint64_t Index);

	std::vector<uint64_t> Runs;
};

struct Core;

struct CoreConnection : Network<CoreConnection>::Connection
{
	Core &Parent;

	struct MediaInfo
	{
		HashType ID;
		uint64_t Size;
	};

	std::queue<MediaInfo> Announce;

	struct
	{
		HashType ID;
		uint64_t Size;
		FilePieces Pieces;
		std::time_point LastResponse;
		bfs::path Path;
		std::fstream File;
	} Request;
	std::vector<MediaInfo> PendingRequests;
	struct FinishedMedia
	{
		HashType ID;
		uint64_t Size;
		bfs::path Path;
	};

	struct
	{
		HashType ID;
		std::fstream File;
		uint64_t Chunk;
	} DataStatus;

	CoreConnection(Core &Parent, std::string cosnt &Host, uint16_t Port, int Socket, ev_loop *EVLoop);

	bool IdleWrite(CoreConnection &Info);

	void HandleTimer(std::time_point const &Now);
	void Handle(NP1V1Clock, uint64_t const &InstanceID, uint64_t const &SystemTime);
	void Handle(NP1V1Prepare, HashType const &MediaID, uint64_t const &Size);
	void Handle(NP1V1Request, HashType const &MediaID, uint64_t const &From);
	void Handle(NP1V1Data, HashType const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes);
	void Handle(NP1V1Play, HashType const &MediaID, uint64_t const &MediaTime, uint64_t const &SystemTime);
	void Handle(NP1V1Stop);
	void Handle(NP1V1Chat, std::string const &Message);
};

struct Core : CallTransferType
{
	Core(void);
	~Core(void);

	// Any thread
	void Open(bool Listen, std::string const &Host, uint16_t Port);

	// Core thread
	void Add(HashType const &MediaID, bfs::path const &Path);
	void Play(HashType const &MediaID, uint64_t Position, uint64_t SystemTime);

	// Callbacks
	std::function<void(uint64_t InstanceID, std::time_point const &SystemTime)> ClockCallback;
	std::function<void(HashType const &MediaID, bfs::path const &Path)> AddCallback;
	std::function<void(HashType const &MediaID, uint64_t MediaTime, std::time_point const &SystemTime)> PlayCallback;
	std::function<void(void)> StopCallback;
	std::function<void(std::string const &Message)> ChatCallback;

	private:
		bfs::path const TempPath;
		uint64_t const ID;

		bool LastPlaying = false;
		uint64_t LastMediaTime;
		uint64_t LastSystemTime;

		std::map<HashType, std::tuple<uint64_t, bfs::path>> Library;

		Network<CoreConnection> Net;
};

#endif
