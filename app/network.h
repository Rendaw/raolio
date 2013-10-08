#ifndef network_h
#define network_h

#include "shared.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ev.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>

template <typename ConnectionType> struct Network
{
	typedef std::function<ConnectionType *(std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop)> CreateConnectionCallback;

	struct Connection
	{
		Connection(std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop, ConnectionType *DerivedThis) :
			Host{Host}, Port{Port}, Socket{Socket}, ReadBuffer{this->Socket}, WriteWatcher{DerivedThis, EVLoop}
		{
			if (this->Socket < 0)
			{
				this->Socket = socket(AF_INET, SOCK_STREAM, 0);
				if (this->Socket < 0) throw SystemError() << "Failed to open socket (" << Host << ":" << Port << "): " << strerror(errno);
				sockaddr_in AddressInfo{};
				AddressInfo.sin_family = AF_INET;
				AddressInfo.sin_addr.s_addr = inet_addr(Host.c_str());
				AddressInfo.sin_port = htons(Port);

				if (connect(this->Socket, reinterpret_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1)
					throw SystemError() << "Failed to connect (" << Host << ":" << Port << "): " << strerror(errno);
			}

			WriteWatcher.Initialize(this->Socket);
		}

		~Connection(void) { assert(Socket >= 0); close(Socket); }

		// Network thread only
		void WakeIdleWrite(void) { WriteWatcher.Wake(); }

		void RawSend(std::vector<uint8_t> const &Data)
		{
			int Sent = write(Socket, &Data[0], Data.size());
			// TODO do something if sent = -1 or != Data.size?
		}

		template <typename MessageType, typename... ArgumentTypes> void Send(MessageType, ArgumentTypes const &... Arguments)
		{
			auto const &Data = MessageType::Write(Arguments...);
			RawSend(Data);
		}

		private:
			friend struct Network<ConnectionType>;
			std::string Host;
			uint16_t Port;
			int Socket;

			struct ReadBufferType
			{
				ReadBufferType(int &Socket) : Socket(Socket) {}

				Protocol::SubVector<uint8_t> Read(size_t Length, size_t Offset = 0)
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
					Buffer.erase(Buffer.begin(), Buffer.begin() + Length);
				}

				int &Socket;
				std::vector<uint8_t> Buffer;
			} ReadBuffer;

			struct WriteWatcherType : ev_io
			{
				WriteWatcherType(ConnectionType *This, struct ev_loop *EVLoop) : This{This}, EVLoop{EVLoop} {}

				void Initialize(int Socket)
				{
					ev_io_init(this, Callback, Socket, EV_WRITE);
					ev_io_start(EVLoop, this);
				}

				void Wake(void) { ev_io_start(EVLoop, this); }

				static void Callback(struct ev_loop *, ev_io *Data, int EventFlags)
				{
					assert(EventFlags & EV_WRITE);
					auto Watcher = static_cast<WriteWatcherType *>(Data);
					if (!Watcher->This->IdleWrite())
						ev_io_stop(Watcher->EVLoop, Data);
				}

				ConnectionType *This;
				struct ev_loop *EVLoop;
			} WriteWatcher;
	};

	struct Listener
	{
		Listener(std::string const &Host, uint16_t Port) : Host{Host}, Port{Port}
		{
			Socket = socket(AF_INET, SOCK_STREAM, 0);
			if (Socket < 0) throw SystemError() << "Failed to open socket (" << Host << ":" << Port << "): " << strerror(errno);
			sockaddr_in AddressInfo{};
			AddressInfo.sin_family = AF_INET;
			AddressInfo.sin_addr.s_addr = inet_addr(Host.c_str());
			AddressInfo.sin_port = htons(Port);

			if (bind(Socket, reinterpret_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1)
				throw SystemError() << "Failed to bind (" << Host << ":" << Port << "): " << strerror(errno);
			if (listen(Socket, 2) == -1) throw SystemError() << "Failed to listen on (" << Port << "): " << strerror(errno);
		}

		~Listener(void) { assert(Socket >= 0); close(Socket); }

		ConnectionType *Accept(CreateConnectionCallback const &CreateConnection, struct ev_loop *EVLoop)
		{
			sockaddr_in AddressInfo{};
			socklen_t AddressInfoLength = sizeof(AddressInfo);
			int ConnectionSocket = accept(Socket, reinterpret_cast<sockaddr *>(&AddressInfo), &AddressInfoLength);
			if (ConnectionSocket == -1) return nullptr; // Log?
			return CreateConnection(inet_ntoa(AddressInfo.sin_addr), AddressInfo.sin_port, ConnectionSocket, EVLoop);
		}

		std::string Host;
		uint16_t Port;
		int Socket;
	};

	template <typename ...MessageTypes> Network(std::tuple<MessageTypes...>, CreateConnectionCallback const &CreateConnection, Optional<float> TimerPeriod)
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		Thread = std::thread{Network::Run<MessageTypes...>, this, CreateConnection, TimerPeriod};
		InitSignal.wait(Lock);
	}

	std::function<void(std::string const &Message)> LogCallback;

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
			OpenQueue.emplace(Listen, Host, Port);
		}
		NotifyOpen();
	}

	void Transfer(std::function<void(void)> const &Call)
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			TransferQueue.emplace(Call);
		}
		NotifyTransfer();
	}

	void Schedule(float Seconds, std::function<void(void)> const &Call)
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			ScheduleQueue.emplace(Seconds, Call);
		}
		NotifySchedule();
	}

	// Network thread only
	std::vector<std::unique_ptr<ConnectionType>> const &GetConnections(void) { return Connections; }

	template <typename MessageType, typename... ArgumentTypes> void Broadcast(MessageType, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		for (auto const &Connection : Connections) Connection->RawSend(Data);
	}

	template <typename MessageType, typename... ArgumentTypes> void Forward(MessageType, Connection const &From, ArgumentTypes const &... Arguments)
	{
		auto const &Data = MessageType::Write(Arguments...);
		for (auto &Connection : Connections)
		{
			if (&*Connection != &From) continue;
			Connection->RawSend(Data);
		}
	}

	private:
		std::mutex Mutex;
		std::condition_variable InitSignal;
		std::thread Thread;
		std::function<void(void)> NotifyOpen;
		std::function<void(void)> NotifyTransfer;
		std::function<void(void)> NotifySchedule;

		// Interface between other and event thread
		mutable bool Die = false;
		struct OpenInfo
		{
			bool Listen;
			std::string Host;
			uint16_t Port;
			OpenInfo(bool Listen, std::string const &Host, uint16_t Port) : Listen{Listen}, Host{Host}, Port{Port} {}
		};
		std::queue<OpenInfo> OpenQueue;
		std::queue<std::function<void(void)>> TransferQueue;
		struct ScheduleInfo
		{
			float Seconds;
			std::function<void(void)> Callback;
			ScheduleInfo(float Seconds, std::function<void(void)> const &Callback) : Callback(Callback) {}
		};
		std::queue<ScheduleInfo> ScheduleQueue;

		// Net-thread only
		std::vector<std::unique_ptr<ConnectionType>> Connections;

		// Used only in thread run
		template <typename DataType> struct EVData : DataType
		{
			std::function<void(EVData<DataType> *)> Callback;
			EVData(std::function<void(EVData<DataType> *)> const &Callback) : Callback(Callback) {}

			static void PreCallback(struct ev_loop *, DataType *Data, int EventFlags)
				{ auto This = static_cast<EVData<DataType> *>(Data); This->Callback(This); }
		};

		// Thread implementation
		template <typename ...MessageTypes> static void Run(Network *This, CreateConnectionCallback const &CreateConnection, Optional<float> TimerPeriod)
		{
			std::unique_lock<std::mutex> Lock(This->Mutex); // Waiting for init signal wait

			// Intermediate event callback storage
			std::vector<std::unique_ptr<EVData<ev_io>>> IOCallbacks;
			std::vector<std::unique_ptr<EVData<ev_timer>>> TimerCallbacks;

			// Set up intermediary event handlers and libev loop
			struct ev_loop *EVLoop = ev_default_loop(0);

			Protocol::Reader<MessageTypes...> Reader;

			std::vector<std::unique_ptr<Listener>> Listeners;

			auto const CreateConnectionWatcher = [&](ConnectionType &Socket)
			{
				auto ReadWatcher = new EVData<ev_io>([&](EVData<ev_io> *)
				{
					Reader.Read(Socket.ReadBuffer, Socket);
					// Ignore errors?
				});
				IOCallbacks.emplace_back(ReadWatcher);
				ev_io_init(ReadWatcher, EVData<ev_io>::PreCallback, Socket.Socket, EV_READ);
				ev_io_start(EVLoop, ReadWatcher);
			};

			EVData<ev_async> AsyncOpenData([&](EVData<ev_async> *)
			{
				/// Exit loop if dying
				if (This->Die) { std::cout << "Got die." << std::endl; ev_break(EVLoop); return; }

				while (true)
				{
					This->Mutex.lock();
					if (This->OpenQueue.empty())
					{
						This->Mutex.unlock();
						break;
					}
					auto Directive = This->OpenQueue.front();
					This->OpenQueue.pop();
					This->Mutex.unlock();

					if (Directive.Listen)
					{
						Listener *Socket = nullptr;
						try { auto Socket = new Listener{Directive.Host, Directive.Port}; }
						catch (SystemError &Error) { if (This->LogCallback) This->LogCallback(Error); continue; }
						Listeners.emplace_back(Socket);

						auto ListenerData = new EVData<ev_io>([&, Socket](EVData<ev_io> *)
						{
							auto ConnectionInfo = Socket->Accept(CreateConnection, EVLoop);
							if (!ConnectionInfo) return;
							std::lock_guard<std::mutex> Lock(This->Mutex);
							This->Connections.push_back(std::unique_ptr<ConnectionType>{ConnectionInfo});
							CreateConnectionWatcher(*ConnectionInfo);
						});
						IOCallbacks.emplace_back(ListenerData);
						ev_io_init(ListenerData, EVData<ev_io>::PreCallback, Socket->Socket, EV_READ);
						ev_io_start(EVLoop, ListenerData);
					}
					else
					{
						ConnectionType *Socket = nullptr;
						try { auto Socket = CreateConnection(Directive.Host, Directive.Port, -1, EVLoop); }
						catch (SystemError &Error) { if (This->LogCallback) This->LogCallback(Error); continue; }
						This->Connections.emplace_back(Socket);
						CreateConnectionWatcher(*Socket);
					}
				}
			});
			ev_async_init(&AsyncOpenData, EVData<ev_async>::PreCallback);
			This->NotifyOpen = [&EVLoop, &AsyncOpenData](void) { ev_async_send(EVLoop, &AsyncOpenData); };

			EVData<ev_async> AsyncTransferData([&](EVData<ev_async> *)
			{
				while (true)
				{
					This->Mutex.lock();
					if (This->TransferQueue.empty())
					{
						This->Mutex.unlock();
						break;
					}
					auto Callback = This->TransferQueue.front();
					This->TransferQueue.pop();
					This->Mutex.unlock();

					Callback();
				}
			});
			ev_async_init(&AsyncTransferData, EVData<ev_async>::PreCallback);
			This->NotifyTransfer = [&](void) { ev_async_send(EVLoop, &AsyncTransferData); };

			EVData<ev_async> AsyncScheduleData([&](EVData<ev_async> *)
			{
				while (true)
				{
					This->Mutex.lock();
					if (This->ScheduleQueue.empty())
					{
						This->Mutex.unlock();
						break;
					}
					auto Directive = This->ScheduleQueue.front();
					This->ScheduleQueue.pop();
					This->Mutex.unlock();

					auto TimerData = new EVData<ev_timer>([&, Directive](EVData<ev_timer> *TimerData)
					{
						Directive.Callback();
						for (auto Callback = TimerCallbacks.begin(); ; Callback++)
						{
							assert(Callback != TimerCallbacks.end());
							if (Callback->get() == TimerData)
							{
								TimerCallbacks.erase(Callback);
								break;
							}
						}
					});
					ev_timer_init(TimerData, EVData<ev_timer>::PreCallback, Directive.Seconds, 0);
					TimerCallbacks.emplace_back(TimerData);
					ev_timer_start(EVLoop, TimerData);
				}
			});
			ev_async_init(&AsyncScheduleData, EVData<ev_async>::PreCallback);
			This->NotifySchedule = [&](void) { ev_async_send(EVLoop, &AsyncScheduleData); };

			if (TimerPeriod)
			{
				auto TimerData = new EVData<ev_timer>([&](EVData<ev_timer> *Timer)
				{
					auto Now = GetNow();
					for (auto &Connection : This->Connections) Connection->HandleTimer(Now);
					ev_timer_set(Timer, *TimerPeriod, 0);
					ev_timer_start(EVLoop, Timer);
				});
				ev_timer_init(TimerData, EVData<ev_timer>::PreCallback, *TimerPeriod, 0);
				TimerCallbacks.emplace_back(TimerData);
				ev_timer_start(EVLoop, TimerData);
			}

			This->InitSignal.notify_all();

			Lock.unlock();

			ev_run(EVLoop, 0);
		}
};

#endif
