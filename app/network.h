#ifndef network_h
#define network_h

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/sctp.h>

template <typename MessageType> struct NetworkChannel;

/* Specialize as follows:
struct NetworkChannel<MessageType1>
{
	static constexpr uint8_t Index = 0;
	static constexpr bool Unordered = false;
};
*/

struct SocketInfo
{
	std::string Host;
	uin16_t Port;
	int Socket;
	std::unique_ptr<Object> ExtraData;
};

template <size_t MaxMessageSize, typename... MessageTypes> struct Network
{
	template <typename CallbackTypes> Network(std::function<void(SocketInfo *Info)> const &AcceptCallback, CallbackTypes const &... Callbacks)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Thread.swap(std::thread{Network::Run, this, AcceptCallback, std::forward<CallbackTypes const &>(Callbacks)...});
		InitSignal.wait(Mutex);
	}

	~Network(void)
	{
		Die = true;
		NotifyThread();
		Thread.join();

		for (auto Info : Listeners) close(Info.Socket);
		for (auto Info : Connections) close(Info.Socket);
	}

	void Open(bool Listen, std::string const &Host, uint16_t Port)
	{
		int Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
		if (Socket < 0) throw SystemError() << "Failed to open socket (" << Host << ":" << ListenPort << "): " << strerror(errno);
		sockaddr_in AddressInfo{};
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = inet_addr(Host.c_str());
		servaddr.sin_port = htons(Port);

		if (Listen)
		{
			if (bind(Socket, static_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1) throw SystemError() << "Failed to bind (" << Host << ":" << ListenPort << "): " << strerror(errno);
			if (listen(listenSock, 2) == -1) throw SystemError() << "Failed to listen on (" << ListenPort << "): " << strerror(errno);

			std::lock_guard<std::mutex> Lock(Mutex);
			auto Info = new SocketInfo{Host, Port, Socket};
			Listeners.push_back(Info);
			NewListeners.push_back(Info);
			NotifyThread();
		}
		else
		{
			int ConnectionSocket = connect(Socket, static_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo));
			if (ConnectionSocket == -1) throw SystemError() << "Failed to connect (" << Host << ":" << Port << "): " << strerror(errno);

			sctp_event_subscribe SCTPConfig{};
			SCTPConfig.sctp_data_io_event = 1;
			setsockopt(SCTPConfig, SOL_SCTP, SCTP_EVENTS, static_cast<void *>(&SCTPConfig), sizeof(SCTPConfig));

			std::lock_guard<std::mutex> Lock(Mutex);
			auto Info = new SocketInfo{Host, Port, Socket};
			Connections.push_back(Info);
			NewConnections.push_back(Info);
			NotifyThread();
		}
	}

	template <typename MessageType, typename... ArgumentTypes> void Send(constexpr MessageType, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		for (auto const &Connection : Connections)
		{
			int Sent = sctp_sendmsg(Socket->Socket,
				&Data[0], Data.size(),
				nullptr, 0, 0,
				NetworkChannel<MessageType>::Unordered ? SCTP_UNORDERED : 0,
				NetworkChannel<MessageType>::Index,
				0, 0);
			// TODO do something if sent = -1 or != Data.size?
		}
	}

	template <typename MessageType, typename... ArgumentTypes> void Reply(constexpr MessageType, SocketInfo *Socket, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		int Sent = sctp_sendmsg(Socket->Socket,
			&Data[0], Data.size(),
			nullptr, 0, 0,
			NetworkChannel<MessageType>::Unordered ? SCTP_UNORDERED : 0,
			NetworkChannel<MessageType>::Index,
			0, 0);
		// TODO do something if sent = -1 or != Data.size?
	}

	template <typename MessageType, typename... ArgumentTypes> void Forward(constexpr MessageType, SocketInfo *From, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		for (auto const &Connection : Connections)
		{
			if (Connection != From) continue;
			int Sent = sctp_sendmsg(Socket->Socket,
				&Data[0], Data.size(),
				nullptr, 0, 0,
				NetworkChannel<MessageType>::Unordered ? SCTP_UNORDERED : 0,
				NetworkChannel<MessageType>::Index,
				0, 0);
			// TODO do something if sent = -1 or != Data.size?
		}
	}

	private:
		std::condition_varible InitSignal;
		std::thread Thread;
		std::function<void(void)> NotifyThread;
		mutable bool Die = false;

		std::mutex Mutex;
		std::vector<std::unique_ptr<SocketInfo>> Listeners;
		std::vector<std::unique_ptr<SocketInfo>> Connections;
		std::vector<SocketInfo *> NewListeners;
		std::vector<SocketInfo *> NewConnections;

		static void Run(Network *This, std::function<void(SocketInfo *Socket)> const &AcceptCallback, std::function<void(SocketInfo *Socket)> const &WriteCallback, CallbackTypes const &... MessageCallbacks)
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

			auto const CreateConnectionWatcher = [&](SocketInfo &Info)
			{
				AcceptCallback(*ConnectionInfo);
				auto ConnectionData = new EVData<ev_io>(WriteCallback, [&](bool Read, bool Write)
				{
					if (Read)
					{
						int ReadFlags;
						sctp_sndrcvinfo ReadInfo;
						int Read = sctp_recvmsg(Info.Socket, &ReadBuffer[0], ReadBuffer.size(), &ReadInfo, &ReadFlags);
						if (Read >= 0) Reader.Read(ReadBuffer, &Info);
						// Ignore errors?
					}

					if (Write) WriteCallback(&Info);
				});
				ev_io_init(ConnectionData, EvData<ev_io>::PreCallback, FileDescriptor, EV_READ | EV_WRITE);
				ev_io_start(EVLoop, ConnectionData);
			};

			EVData<ev_async> AsyncData([&](void)
			{
				/// Exit loop if dying
				if (Die) ev_break_all(EVLoop);

				/// Created listeners
				std::lock_guard<std::mutex> Lock(This->Mutex);
				for (auto ListenerInfo : This->NewListeners)
				{
					auto ListenerData = new EVData<ev_io>([ListenerInfo, &]
					{
						/// Accepted connection
						sockaddr_in AddressInfo{};
						int AddressInfoLength = sizeof(AddressInfo);
						int ConnectionSocket = accept(ListenerInfo->Socket, &AddressInfo, &AddressInfoLength);
						if (ConnectionSocket == -1) return; // Log?

						std::lock_guard<std::mutex> Lock(This->Mutex);
						auto ConnectionInfo = new SocketInfo{};
						ConnectionInfo->Host = inet_ntoa(AddressInfo.sin_addr);
						ConnectionInfo->Port = AddressInfo.sin_port;
						ConnectionInfo->Socket = ConnectionSocket;
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

#endif
