#ifndef network_h
#define network_h

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/sctp.h>

template <typename ConnectionType> struct Network
{
	typedef std::function<ConnectionType *(Network<ConnectionType> &Parent, std::string const &Host, uint16_t Port, int Socket, ev_loop *EVLoop)> CreateConnectionCallback;
	
	struct Connection
	{
		Connection(std::string const &Host, uint16_t Port, int Socket, ev_loop *EVLoop, ConnectionType *DerivedThis) : 
			Host{Host}, Port{Port}, Socket{Socket}, ReadBuffer{this->Socket}, WriteWatcher{DerivedThis, EVLoop}
		{
			if (Socket < 0)
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
			
			WriteWatcher.Initialize();
		}

		~Connection(void) { assert(Socket >= 0); close(Socket); }
		
		// Network thread only
		void WakeIdleWrite(void) { WriteWatcher.Wake(); }
		
		template <typename MessageType, typename... ArgumentTypes> void Send(constexpr MessageType, ArgumentTypes const &... Arguments)
		{
			auto const &Data = MessageType::Write(Arguments...);
			int Sent = write(Socket, &Data[0], Data.size());
			// TODO do something if sent = -1 or != Data.size?
		}

		private:
			friend struct Manager;
			std::string Host;
			uin16_t Port;
			int Socket;
			
			struct ReadBufferType
			{
				ReadBufferType(int &Socket) : Socket(Socket) {}

				SubVector<uint8_t> Read(size_t Length, size_t Offset = 0)
				{
					assert(Socket >= 0);
					if (Length <= Buffer.size()) return {Buffer, Offset, Length};
					auto const Difference = Length - Buffer.size();
					auto const OriginalLength = Buffer.size();
					Buffer.resize(Length);
					int Count = read(Socket, &Buffer[OriginalLength], Difference);
					if (Count <= 0) { Buffer.resize(OriginalLength); return {}; }
					if (Count < Difference) { Buffer.resize(OriginalLength + Count); return {}; }
					return {Buffer, Offset, Length};
				}

				void Consume(size_t Length)
				{
					assert(Socket >= 0);
					assert(Length <= Buffer.size());
					std::erase(Buffer.begin(), Buffer.begin() + Length);
				}

				int &Socket;
				std::vector<uint8_t> Buffer;
			} ReadBuffer
			
			struct WriteWatcherType : ev_io
			{
				WriteWatcherType(ConnectionType *This, ev_loop *EVLoop) : This{This}, EVLoop{EVLoop} {}
				
				void Initialize(int Socket)
				{
					ev_io_init(this, Callback, Socket, EV_WRITE);
					ev_io_start(EVLoop, this);
				}
				
				void Wake(void) { ev_io_start(EVLoop, this); }

				static void Callback(ev_loop *, ev_io *Data, int EventFlags)
				{
					assert(EventFlags & EV_WRITE);
					auto Watcher = static_cast<WriteWatcherType *>(Data);
					if (!Watcher->This->IdleWrite())
						ev_io_stop(Watcher->EVLoop, Data);
				}
				
				ConnectionType *This;
				ev_loop *EVLoop;
			} WriteWatcher;
	};

	struct Listener
	{
		Listener(std::string const &Host, uint16_t Port) : Host{Host}, Port{Port}
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

		~Listener(void) { assert(Socket >= 0); close(Socket); }

		private:
			friend struct Manager;

			template <typename ConnectionType> std::unique_ptr<ConnectionType> Accept(CreateConnectionCallback const &CreateConnection, ev_loop *EVLoop)
			{
				sockaddr_in AddressInfo{};
				int AddressInfoLength = sizeof(AddressInfo);
				int ConnectionSocket = accept(ListenerInfo->Socket, &AddressInfo, &AddressInfoLength);
				if (ConnectionSocket == -1) return nullptr; // Log?
				return CreateConnection(inet_ntoa(AddressInfo.sin_addr), AddressInfo.sin_port, ConnectionSocket, EVLoop);
			}

			std::string Host;
			uin16_t Port;
			int Socket;
	};

	template <typename ...MessageTypes> Network(CreateConnectionCallback const &CreateConnection, Optional<float> TimerPeriod)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Thread.swap(std::thread{Network::Run<MessageTypes...>, this, CreateConnection, TimerPeriod});
		InitSignal.wait(Mutex);
	}

	~Network(void)
	{
		Die = true;
		NotifyOpen();
		Thread.join();
	}

	// Thread safe
	void Open(bool Listen, std::string const &Host, uint16_t Port)
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			OpenQueue.push_back({Listen, Host, Port});
		}
		NotifyOpen();
	}

	protected:
		// Network thread only
		template <typename MessageType, typename... ArgumentTypes> void Broadcast(constexpr MessageType, ArgumentTypes const &... Arguments)
		{
			auto const &Data = MessageType::Write(Arguments...);
			for (auto const &Connection : Connections) Connection->Send(MessageType, std::forward<ArgumentTypes const &>(Arguments)...);
		}

		// Network thread only
		template <typename MessageType, typename... ArgumentTypes> void Forward(constexpr MessageType, Connection const &From, ArgumentTypes const &... Arguments)
		{
			auto const &Data = MessageType::Write(Arguments...);
			for (auto const &Connection : Connections)
			{
				if (&*Connection != &From) continue;
				Connection->Send(MessageType, std::forward<ArgumentTypes const &>(Arguments)...);
			}
		}

	private:
		std::mutex Mutex;
		std::condition_varible InitSignal;
		std::thread Thread;
		std::function<void(void)> NotifyOpen;

		// Interface between other and event thread
		mutable bool Die = false;
		struct OpenInfo { bool Listen; std::string Host; uint16_t Port; };
		std::queue<OpenInfo> OpenQueue;

		// Thread implementation
		template <typename ...MessageTypes> static void Run(Network *This, CreateConnectionCallback const &CreateConnection, Optional<float> TimerPeriod)
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
					(*static_cast<EVData<DataType> *>(Data))->Callback();
				}
			};

			std::vector<std::unique_ptr<EVData<ev_io>>> IOCallbacks;

			// Set up intermediary event handlers and libev loop
			ev_loop *EVLoop = ev_default_loop(0);

			Protocol::Reader<MessageTypes> Reader{std::forward<CallbackTypes const &>(MessageCallbacks)...};

			std::vector<std::unique_ptr<Listener>> Listeners;
			std::vector<std::unique_ptr<Connection>> Connections;

			auto const CreateConnectionWatcher = [&](Connection &Socket)
			{
				{
					auto ReadWatcher = new EVData<ev_io>([&](void)
					{
						Reader.Read(Socket.ReadBuffer, Socket)
						// Ignore errors?
					});
					IOCallbacks.push_back(ReadWatcher);
					ev_io_init(ReadWatcher, EvData<ev_io>::PreCallback, FileDescriptor, EV_READ);
					ev_io_start(EVLoop, ReadWatcher);
				}

				ev_io_start(EVLoop, &Socket.WriteWatcher);
				{
					EVData<ev_io> *WriteWatcher = new EVData<ev_io>([&](void)
					{
						if (!Socket.IdleWrite())
							ev_io_stop(EVLoop, WriteWatcher);
					});
					ev_io_init(ReadWatcher, EvData<ev_io>::PreCallback, FileDescriptor, EV_WRITE);
					IOCallbacks.push_back(WriteWatcher);
					Socket.Watcher = WriteWatcher;
				}
			};

			EVData<ev_async> AsyncOpenData([&](void)
			{
				/// Exit loop if dying
				if (Die) ev_break(EVLoop);

				while (true)
				{
					This->Mutex.lock();
					if (OpenQueue.empty())
					{
						This->Mutex.unlock();
						break;
					}
					auto Directive = OpenQueue.front();
					OpenQueue.pop();
					This->Mutex.unlock();

					if (Directive.Listen)
					{
						Listener *Socket = nullptr;
						try { auto Socket = new Listener{Directive.Host, Directive.Port}; }
						catch (...) { continue; } // TODO Log or warn?
						Listeners.push_back(Socket);

						auto ListenerData = new EVData<ev_io>([Socket, &]
						{
							auto ConnectionInfo = Socket->Accept(CreateConnection, EVLoop);
							if (!ConnectionInfo) return;
							std::lock_guard<std::mutex> Lock(This->Mutex);
							This->Connections.push_back(ConnectionInfo);
							CreateConnectionWatcher(*ConnectionInfo);
						});
						IOCallbacks.push_back(ListenerData});
						ev_io_init(ListenerData, EvCallback<ev_io>, Socket->Socket, EV_READ);
						ev_io_start(EVLoop, ListenerData);
					}
					else
					{
						Connection *Socket = nullptr;
						try { auto Socket = CreateConnection(Directive.Host, Directive.Port, -1, EVLoop); }
						catch (...) { continue; } // TODO Log or warn?
						Connections.push_back(Socket);
						CreateConnectionWatcher(*Socket);
					}
				}
			});
			ev_async_init(&AsyncOpenData, EVCallback<ev_async>);
			This->NotifyOpen = [&EVLoop, &AsyncOpenData](void) { ev_async_send(EVLoop, AsyncOpenData); }

			std::unique_ptr<EVData<ev_timer>> TimerData;
			if (TimerPeriod)
			{
				TimerData.reset(new EVData<ev_timer>([&]
				{
					for (auto Connection : Connections) Connection->HandleTimer();
					Directive.Callback();
					ev_timer_set(TimerData, *TimerPeriod, 0);
					ev_timer_start(EVLoop, TimerData);
				}));
				ev_timer_init(*&TimerData, EvCallback<ev_timer>);
				ev_timer_set(*&TimerData, *TimerPeriod, 0);
				ev_timer_start(EVLoop, *&TimerData);
			}

			This->InitSignal.notify_all();

			ev_run(EVLoop, 0);
		}
};

}

#endif
