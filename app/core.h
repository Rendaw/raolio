#ifndef core_h
#define core_h

#include "protocol.h"
#include "protocoloperations.h"
#include "network.h"
#include "hash.h"
#include <map>

constexpr uint64_t ChunkSize = 512; // Set for all protocol versions

typedef StrictType(uint64_t) MediaTimeT;

DefineProtocol(NetProto1)

DefineProtocolVersion(NP1V1, NetProto1)
DefineProtocolMessage(NP1V1Clock, NP1V1, void(uint64_t InstanceID, uint64_t SystemTime))
DefineProtocolMessage(NP1V1Prepare, NP1V1, void(HashT MediaID, std::string Extension, uint64_t Size, std::string DefaultTitle))
DefineProtocolMessage(NP1V1Request, NP1V1, void(HashT MediaID, uint64_t From))
DefineProtocolMessage(NP1V1Data, NP1V1, void(HashT MediaID, uint64_t Chunk, std::vector<uint8_t> Bytes))
DefineProtocolMessage(NP1V1Remove, NP1V1, void(HashT MediaID))
DefineProtocolMessage(NP1V1Play, NP1V1, void(HashT MediaID, MediaTimeT MediaTime, uint64_t SystemTime))
DefineProtocolMessage(NP1V1Stop, NP1V1, void(void))
DefineProtocolMessage(NP1V1Chat, NP1V1, void(std::string Message))

struct FilePieces
{
	FilePieces(void);
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

	bool SentPlayState;

	struct MediaInfo
	{
		HashT ID;
		std::string Extension;
		uint64_t Size;
		std::string DefaultTitle;
		MediaInfo(HashT const &ID, std::string const &Extension, uint64_t const &Size, std::string const &DefaultTitle) : ID(ID), Extension{Extension}, Size{Size}, DefaultTitle{DefaultTitle} {}
	};

	std::queue<MediaInfo> Announce;

	struct
	{
		HashT ID;
		uint64_t Size;
		FilePieces Pieces;
		uint64_t LastResponse; // Time, ms since epoch
		PathT Path;
		FILE *File = nullptr;
		unsigned int Attempts;
		std::string DefaultTitle;
	} Request;
	std::queue<MediaInfo> PendingRequests;

	struct
	{
		HashT ID;
		FILE *File = nullptr;
		uint64_t Chunk;
	} Response;

	CoreConnection(Core &Parent, std::string const &Host, uint16_t Port, uv_tcp_t *Watcher, std::function<void(CoreConnection &Socket)> const &ReadCallback);

	bool IdleWrite(void);

	void HandleTimer(uint64_t const &Now);
	void Handle(NP1V1Clock, uint64_t const &InstanceID, uint64_t const &SystemTime);
	void Handle(NP1V1Prepare, HashT const &MediaID, std::string const &Extension, uint64_t const &Size, std::string const &DefaultTitle);
	void Handle(NP1V1Request, HashT const &MediaID, uint64_t const &From);
	void Handle(NP1V1Data, HashT const &MediaID, uint64_t const &Chunk, std::vector<uint8_t> const &Bytes);
	void Handle(NP1V1Remove, HashT const &MediaID);
	void Handle(NP1V1Play, HashT const &MediaID, MediaTimeT const &MediaTime, uint64_t const &SystemTime);
	void Handle(NP1V1Stop);
	void Handle(NP1V1Chat, std::string const &Message);

	bool RequestNext(void);

	void Remove(HashT const &MediaID);
};

struct Core : CallTransferType
{
	struct PlayStatus
	{
		bool Playing;
		HashT MediaID;
		MediaTimeT MediaTime;
		uint64_t SystemTime;
	};

	Core(bool PruneOldItems);
	~Core(void);

	// Any thread
	void Open(bool Listen, std::string const &Host, uint16_t Port);
	void Transfer(std::function<void(void)> const &Call) override;
	void Schedule(float Seconds, std::function<void(void)> const &Call);

	// Core thread only
	void Add(HashT const &MediaID, size_t Size, PathT const &Path);
	void Remove(HashT const &MediaID);
	void Play(HashT const &MediaID, MediaTimeT Position, uint64_t SystemTime);
	void Stop(void);
	void Chat(std::string const &Message);

	PlayStatus const &GetPlayStatus(void) const;

	// Callbacks
	enum LogPriority { Important, Unimportant, Debug, Useless };
	std::function<void(LogPriority Priority, std::string const &Message)> LogCallback;

	std::function<void(uint64_t InstanceID, uint64_t const &SystemTime)> ClockCallback;
	std::function<void(HashT const &MediaID, PathT const &Path, std::string const &DefaultTitle)> AddCallback;
	std::function<void(HashT const &MediaID)> RemoveCallback;
	std::function<void(HashT const &MediaID, MediaTimeT MediaTime, uint64_t const &SystemTime)> PlayCallback;
	std::function<void(void)> StopCallback;
	std::function<void(std::string const &Message)> ChatCallback;

	private:
		friend struct CoreConnection;

		void RemoveInternal(HashT const &MediaID);

		PathT const TempPath;
		uint64_t const ID;

		bool const Prune;

		PlayStatus Last;

		struct LibraryInfo
		{
			uint64_t Size;
			PathT Path;
			std::string DefaultTitle;
			LibraryInfo(uint64_t Size, PathT const &Path, std::string const &DefaultTitle) : Size{Size}, Path{Path}, DefaultTitle{DefaultTitle} {}
		};
		std::map<HashT, LibraryInfo> Library;

		Network<CoreConnection> Net;
};

#endif
