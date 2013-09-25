#ifndef network_h
#define network_h

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/sctp.h>

namespace Network
{

struct SocketInfo
{
	inline SocketInfo(std::string const &Host, uint16_t Port, int Socket) : Host(Host), Port(Port), Socket(Socket) {}
	virtual ~SocketInfo(void);
	
	private:
		friend class Manager;
		
		std::string Host;
		uin16_t Port;
		int Socket;
};

struct Connection : SocketInfo
{
	Connection(std::string cosnt &Host, uint16_t Port, int Socket) : Host(Host), Port(Port), Socket(Socket) {}
	Connection(std::string const &Host, uint16_t Port) : SocketInfo{Host, Port}
	{
		Info.Socket = socket(AF_INET, SOCK_STREAM, 0);
		if (Socket < 0) throw SystemError() << "Failed to open socket (" << Host << ":" << ListenPort << "): " << strerror(errno);
		sockaddr_in AddressInfo{};
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = inet_addr(Host.c_str());
		servaddr.sin_port = htons(Port);

		if (connect(Socket, static_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1)
			throw SystemError() << "Failed to connect (" << Host << ":" << Port << "): " << strerror(errno);
	}

	template <typename MessageType, typename... ArgumentTypes> void Send(constexpr MessageType, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		int Sent = write(Socket, &Data[0], Data.size());
		// TODO do something if sent = -1 or != Data.size?
	}
	
	virtual bool WriteReady(void) { return false; }
	
	void WakeIdleWrite(void)
	{
		if (WriteActive) return;
		WriteActive = WriteReady();
	}
	
	bool Read(std::vector<uint8_t> &Buffer)
	{
		int Read = read(Socket, &Buffer[0], Buffer.size());
		if (Read < 0) return false;
		return true;
	}
	
	private:
		friend struct Manager;
		bool WriteActive = true;
};

struct Listener
{
	Listener(std::string const &Host, uint16_t Port) : SocketInfo{Host, Port}
	{
		Socket = socket(AF_INET, SOCK_STREAM, 0);
		if (Socket < 0) throw SystemError() << "Failed to open socket (" << Host << ":" << ListenPort << "): " << strerror(errno);
		sockaddr_in AddressInfo{};
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = inet_addr(Host.c_str());
		servaddr.sin_port = htons(Port);

		if (bind(Socket, static_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1) 
			throw SystemError() << "Failed to bind (" << Host << ":" << Port << "): " << strerror(errno);
		if (listen(Sock, 2) == -1) throw SystemError() << "Failed to listen on (" << ListenPort << "): " << strerror(errno);
	}
	
	std::unique_ptr<SocketInfo> Accept(void)
	{
		sockaddr_in AddressInfo{};
		int AddressInfoLength = sizeof(AddressInfo);
		int ConnectionSocket = accept(ListenerInfo->Socket, &AddressInfo, &AddressInfoLength);
		if (ConnectionSocket == -1) return nullptr; // Log?
		return CreateConnection(inet_ntoa(AddressInfo.sin_addr), AddressInfo.sin_port, ConnectionSocket);
	}
	
	virtual std::unique_ptr<SocketInfo> CreateConnection(std::string const &Host, uint16_t Port, int Socket)
		{ return new Connection{Host, Port, Socket}; }
};

template <typename SocketInfoType, size_t MaxMessageSize, typename... MessageTypes> struct Manager
{
	template <typename CallbackTypes> Manager(CallbackTypes const &... Callbacks)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Thread.swap(std::thread{Network::Run, this, std::forward<CallbackTypes const &>(Callbacks)...});
		InitSignal.wait(Mutex);
	}

	~Manager(void)
	{
		Die = true;
		NotifyThread();
		Thread.join();
	}

	void Open(std::unique_ptr<Connection> &&Connection)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		NewConnections.push_back(*&Connection);
		Connections.push_back(std::move(Connection));
		NotifyThread();
	}
	
	void Open(std::unique_ptr<Listener> &&Listener)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		NewListeners.push_back(*&Listener);
		Listeners.push_back(std::move(Listener));
		NotifyThread();
	}
	
	template <typename MessageType, typename... ArgumentTypes> void Broadcast(constexpr MessageType, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		for (auto const &Connection : Connections) Connection->Send(MessageType, std::forward<ArgumentTypes const &>(Arguments)...);
	}

	template <typename MessageType, typename... ArgumentTypes> void Forward(constexpr MessageType, SocketInfo const &From, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		for (auto const &Connection : Connections) 
		{
			if (&*Connection != &From) continue;
			Connection->Send(MessageType, std::forward<ArgumentTypes const &>(Arguments)...);
		}
	}
	
	private:
		std::condition_varible InitSignal;
		std::thread Thread;
		std::function<void(void)> NotifyThread;
		mutable bool Die = false;

		std::mutex Mutex;
		std::vector<std::unique_ptr<Listener>> Listeners;
		std::vector<std::unique_ptr<Connection>> Connections;
		std::vector<Listener *> NewListeners;
		std::vector<Connection *> NewConnections;

		static void Run(Network *This, std::function<void(SocketInfo *Socket)> const &WriteCallback, CallbackTypes const &... MessageCallbacks)
		{
			std::lock_guard<std::mutex> Lock(This->Mutex); // Waiting for init signal wait

			// Intermediate event callback storage
			template <typename DataType> struct EVData : DataType
			{
				std::function<void(void)> Callback;
				EVData(std::function<void(void)> const &Callback) : Callback(Callback) {}

				static void PreCallback(ev_loop *, DataType *Data, int EventFlags)
				{
					assert(EventFlags & EV_READ);
					(*static_cast<EVData<DataType> *>(Data))->Callback(EventFlags & EV_READ, EventFlags & EV_WRITE);
				}
			};

			std::vector<std::unique_ptr<EVData<ev_io>>> IOCallbacks;
			std::vector<std::unique_ptr<EVData<ev_timer>>> TimerCallbacks;

			// Set up intermediary event handlers and libev loop
			ev_loop *EVLoop = ev_default_loop(0);

			Protocol::Reader<MessageTypes> Reader{std::forward<CallbackTypes const &>(MessageCallbacks)...};
			std::vector<uint8_t> ReadBuffer(MaxMessageSize);

			auto const CreateConnectionWatcher = [&](Connection &Info)
			{
				auto ConnectionData = new EVData<ev_io>(WriteCallback, [&](bool Read, bool Write)
				{
					if (Read)
					{
						if (Info.Read(ReadBuffer))
							Reader.Read(ReadBuffer, &Info);
						// Ignore errors?
					}

					if (Write) 
					{
						if (Info.WriteActive)
							Info.WriteActive = WriteCallback(&Info);
					}
				});
				ev_io_init(ConnectionData, EvData<ev_io>::PreCallback, FileDescriptor, EV_READ | EV_WRITE);
				ev_io_start(EVLoop, ConnectionData);
			};

			EVData<ev_async> AsyncData([&](void)
			{
				/// Exit loop if dying
				if (Die) ev_break(EVLoop);

				std::lock_guard<std::mutex> Lock(This->Mutex);
				
				/// Created listeners
				for (auto ListenerInfo : This->NewListeners)
				{
					auto ListenerData = new EVData<ev_io>([ListenerInfo, &]
					{
						auto ConnectionInfo = ListenerInfo->Accept();
						if (!ConnectionInfo) return;
						std::lock_guard<std::mutex> Lock(This->Mutex);
						This->Connections.push_back(ConnectionInfo);
						CreateConnectionWatcher(*ConnectionInfo);
					});
					IOCallbacks.push_back(ListenerData});
					ev_io_init(ListenerData, EvCallback<ev_io>, ListenerInfo->Socket, EV_READ);
					ev_io_start(EVLoop, ListenerData);
				}
				NewListeners.clear();

				/// Created connections
				for (auto ConnectionInfo : This->NewConnections)
				{
					CreateConnectionWatcher(*ConnectionInfo);
				}
				NewConnections.clear();
			});
			ev_async_init(&AsyncData, EVCallback<ev_async>);

			NotifyThread = [&EVLoop, &AsyncData](void) { ev_async_send(EVLoop, AsyncData); }
			This->InitSignal.notify_all();

			ev_run(EVLoop, 0);
		}
};

}

#endif
