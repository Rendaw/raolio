#ifndef network_h
#define network_h

#include "shared.h"
#include "type.h"
#include "translation/translation.h"

#include <unistd.h>
extern "C"
{
	#include <uv.h>
}
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <list>

template <typename ConnectionType> struct Network
{
	template <typename DataType, typename ...ExtraTypes> struct UVData : DataType
	{
		std::function<void(UVData<DataType, ExtraTypes...> *, ExtraTypes ...)> Callback;
		UVData(void) {}
		UVData(decltype(Callback) const &Callback) : Callback(Callback) {}
		template <typename Whatever> static void Fix(Whatever *Data, ExtraTypes ...Extras)
		{
			auto This = reinterpret_cast<UVData<DataType, ExtraTypes...> *>(Data);
			This->Callback(This, Extras...);
		}
	};
	template <typename DataType> struct UVWatcherData : DataType
	{
		std::function<void(UVWatcherData<DataType> *)> Callback;
		UVWatcherData(void) {}
		UVWatcherData(std::function<void(UVWatcherData<DataType> *)> const &Callback) : Callback(Callback) {}

		static void PreCallback(DataType *Data)
			{ auto This = static_cast<UVWatcherData<DataType> *>(Data); This->Callback(This); }
	};

	typedef std::function<ConnectionType *(std::string const &Host, uint16_t Port, uv_tcp_t *Watcher, std::function<void(ConnectionType &Socket)> const &ReadCallback)> CreateConnectionCallback;

	struct Connection
	{
		Connection(std::string const &Host, uint16_t Port, uv_tcp_t *Watcher, std::function<void(ConnectionType &Socket)> const &ReadCallback, ConnectionType &DerivedThis) :
			Dead{false}, HasIdleData{false}, Host{Host}, Port{Port}, Watcher{Watcher}, ReadBuffer{*this}, ReadCallback{ReadCallback}, This(DerivedThis), WriteCounter(0)
		{
			assert(Watcher);
			Watcher->data = &DerivedThis;
			uv_read_start(reinterpret_cast<uv_stream_t *>(Watcher),
				[](uv_handle_t *Watcher, size_t Length, uv_buf_t *Buffer)
				{
					ConnectionType *This = static_cast<ConnectionType *>(Watcher->data);
					This->ReadBuffer.Allocate(Length, Buffer);
				},
				[](uv_stream_t *Watcher, ssize_t Length, uv_buf_t const *Buffer)
				{
					ConnectionType *This = static_cast<ConnectionType *>(Watcher->data);
					if (Length < 0)
					{
						This->Die();
						return;
					}
					if (Length == 0) return;
					This->ReadBuffer.Filled(Length);
					This->ReadCallback(*This);
				}
			);
		}

		~Connection(void) { if (!Dead) Die(); }

		// Network thread only
		bool IsDead(void) { return Dead; }
		uint64_t GetDiedAt(void) { assert(Dead); return DiedAt; }

		void WakeIdleWrite(void) { if (Dead) return; if (HasIdleData) return; HasIdleData = true; HasIdleData = This.IdleWrite(); }

		void RawSend(std::vector<uint8_t> const &Data)
		{
			if (Dead) return;
			uv_buf_t Buffers[1]{};

			struct WriteRequestInfo : uv_write_t
			{
				ConnectionType &This;
				std::vector<uint8_t> Data;
				uint64_t WriteID;
				WriteRequestInfo(ConnectionType &This, std::vector<uint8_t> const &Data, uint64_t WriteID) : This(This), Data(Data), WriteID(WriteID) {}
			};
			auto Request = new WriteRequestInfo{This, Data, ++WriteCounter};

			Buffers[0].base = reinterpret_cast<char *>(&Request->Data[0]);
			Buffers[0].len = Request->Data.size();
			int Error = uv_write(Request, reinterpret_cast<uv_stream_t *>(Watcher), Buffers, 1,
				[](uv_write_t *Request, int Error)
				{
					std::unique_ptr<WriteRequestInfo> Info(static_cast<WriteRequestInfo *>(Request));
					if (Error) { Info->This.Die(); return; }
					if ((Info->WriteID == Info->This.WriteCounter) && Info->This.HasIdleData) Info->This.HasIdleData = Info->This.IdleWrite();
				}
			);
			if (Error) Die();
		}

		template <typename MessageType, typename... ArgumentTypes> void Send(MessageType, ArgumentTypes const &... Arguments)
		{
			if (Dead) return;
			auto Data = MessageType::Write(Arguments...);
			RawSend(std::move(Data));
		}

		void Die(void)
		{
			assert(!Dead);

			assert(Watcher);
			uv_read_stop(reinterpret_cast<uv_stream_t *>(Watcher));
			uv_close(reinterpret_cast<uv_handle_t *>(Watcher), [](uv_handle_t *Watcher) { delete reinterpret_cast<uv_tcp_t *>(Watcher); });

			Dead = true;
			DiedAt = GetNow();
		}

		private:
			friend struct Network<ConnectionType>;

			bool Dead;
			uint64_t DiedAt;
			bool HasIdleData;

			std::string Host;
			uint16_t Port;
			uv_tcp_t *Watcher;

			struct ReadBufferType
			{
				ReadBufferType(Connection &This) : This(This), Used(0) {}

				void Allocate(size_t Length, uv_buf_t *Out)
				{
					if (Length == 0)
					{
						Out->len = 0;
						return;
					}
					if (Buffer.size() - Used < Length)
						Buffer.resize(Used + Length);
					Out->base = reinterpret_cast<char *>(&Buffer[Used]);
					Out->len = Length;
				}

				void Filled(size_t Length)
					{ Used += Length; }

				Protocol::SubVector<uint8_t> Read(size_t Length, size_t Offset = 0)
				{
					if (Length == 0) return {};
					if (Offset + Length > Used) return {};
					return {Buffer, Offset, Length};
				}

				void Consume(size_t Length)
				{
					assert(This.Watcher >= 0);
					assert(Length > 0);
					assert(Length <= Used);
					Buffer.erase(Buffer.begin(), Buffer.begin() + Length);
					Used -= Length;
				}

				Connection &This;
				size_t Used;
				std::vector<uint8_t> Buffer;
			} ReadBuffer;

			std::function<void(ConnectionType &Socket)> ReadCallback;

			ConnectionType &This;

			uint64_t WriteCounter;
	};

	struct Listener
	{
		Listener(std::string const &Host, uint16_t Port) : Host{Host}, Port{Port}, Watcher(new UVData<uv_tcp_t>)
		{
			uv_tcp_init(uv_default_loop(), Watcher);
			int Error;
			struct sockaddr_in Address;
			if ((Error = uv_ip4_addr(Host.c_str(), Port, &Address)))
				throw ConstructionErrorT() << Local("Invalid address (^0:^1)", Host, Port);
			if ((Error = uv_tcp_bind(Watcher, reinterpret_cast<sockaddr *>(&Address), 0)))
				throw ConstructionErrorT() << Local("Failed to bind (^0:^1): ^2", Host, Port, uv_strerror(Error));
			if ((Error = uv_listen(reinterpret_cast<uv_stream_t *>(Watcher), 2, [](uv_stream_t *Data, int Error) { assert(Error == 0); UVData<uv_tcp_t>::Fix(Data); })))
				throw ConstructionErrorT() << Local("Failed to listen on (^0:^1): ^2", Host, Port, uv_strerror(Error));
		}

		~Listener(void) { uv_close(reinterpret_cast<uv_handle_t *>(Watcher), [](uv_handle_t *Watcher) { delete reinterpret_cast<UVData<uv_tcp_t> *>(Watcher); }); }

		std::string Host;
		uint16_t Port;
		UVData<uv_tcp_t> *Watcher;
	};

	template <typename ...MessageTypes> Network(std::tuple<MessageTypes...>, CreateConnectionCallback const &CreateConnection, OptionalT<float> TimerPeriod)
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
	std::list<std::unique_ptr<ConnectionType>> const &GetConnections(void) { return Connections; }

	template <typename MessageType, typename... ArgumentTypes> void Broadcast(MessageType, ArgumentTypes const &... Arguments)
	{
		auto const Data = MessageType::Write(Arguments...);
		for (auto const &Connection : Connections)
			Connection->RawSend(Data);
	}

	template <typename MessageType, typename... ArgumentTypes> void Forward(MessageType, Connection const &From, ArgumentTypes const &... Arguments)
	{
		auto const Data = MessageType::Write(Arguments...);
		for (auto &Connection : Connections)
		{
			if (&*Connection == &From) continue;
			Connection->RawSend(Data);
		}
	}

	OptionalT<uint64_t> IdleSince(void)
	{
		auto Out = DeletedIdleSince;
		for (auto &Connection : Connections)
		{
			if (Connection->IsDead() && (!Out || (Connection->GetDiedAt() > *Out)))
				Out = Connection->GetDiedAt();
		}
		return Out;
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
			ScheduleInfo(float Seconds, std::function<void(void)> const &Callback) : Seconds{Seconds}, Callback{Callback} {}
		};
		std::queue<ScheduleInfo> ScheduleQueue;

		// Net-thread only
		OptionalT<uint64_t> DeletedIdleSince;
		std::list<std::unique_ptr<ConnectionType>> Connections;

		// Thread implementation
		template <typename ...MessageTypes> static void Run(Network *This, CreateConnectionCallback const &CreateConnection, OptionalT<float> TimerPeriod)
		{
			std::unique_lock<std::mutex> Lock(This->Mutex); // Waiting for init signal wait

			// Intermediate event callback storage
			auto const TimerCallbackFree = [](UVWatcherData<uv_timer_t> *Data) { uv_close(reinterpret_cast<uv_handle_t *>(Data), [](uv_handle_t *Data) { delete reinterpret_cast<UVWatcherData<uv_timer_t> *>(Data); }); };
			std::list<std::unique_ptr<UVWatcherData<uv_timer_t>, decltype(TimerCallbackFree)>> TimerCallbacks;

			// Set up intermediary event handlers and libev loop
			Protocol::Reader<MessageTypes...> Reader;

			std::list<std::unique_ptr<Listener>> Listeners;

			auto const ReadCallback = [&](ConnectionType &Socket)
			{
				Protocol::ReadResult Result;
				do
				{
					Result = Reader.Read(Socket.ReadBuffer, Socket);
					if (Result == Protocol::Error)
					{
						std::cout << "::REND:: failed to read." << std::endl;
					}
				} while (Result == Protocol::Stop);
			};

			auto const CleanConnections = [&](void)
			{
				for (auto Connection = This->Connections.begin(); Connection != This->Connections.end();)
				{
					if ((*Connection)->IsDead())
					{
						if (!This->DeletedIdleSince || ((*Connection)->GetDiedAt() > *This->DeletedIdleSince))
							This->DeletedIdleSince = (*Connection)->GetDiedAt();
						Connection = This->Connections.erase(Connection);
					}
					else ++Connection;
				}
			};

			auto AsyncOpenData = new UVWatcherData<uv_async_t>([&](UVWatcherData<uv_async_t> *)
			{
				/// Exit loop if dying
				if (This->Die) { uv_stop(uv_default_loop()); return; }

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
						try { Socket = new Listener{Directive.Host, Directive.Port}; }
						catch (ConstructionErrorT &Error) { if (This->LogCallback) This->LogCallback(Error); continue; }
						Listeners.emplace_back(Socket);
						Socket->Watcher->Callback = [&, Socket](UVData<uv_tcp_t> *ListenWatcher)
						{
							CleanConnections();

							auto Watcher = new uv_tcp_t;
							uv_tcp_init(uv_default_loop(), Watcher);

							uv_accept(reinterpret_cast<uv_stream_t *>(Socket->Watcher), reinterpret_cast<uv_stream_t *>(Watcher));

							auto *ConnectionInfo = CreateConnection(Socket->Host, Socket->Port, Watcher, ReadCallback);

							std::lock_guard<std::mutex> Lock(This->Mutex);
							This->Connections.push_back(std::unique_ptr<ConnectionType>{ConnectionInfo});
						};
					}
					else
					{
						using AddressRequestInfo = UVData<uv_getaddrinfo_t, int, struct addrinfo *>;
						auto HostString = new std::string(Directive.Host);
						auto PortString = new std::string(StringT() << Directive.Port);
						auto AddressRequest = new AddressRequestInfo { [=, &This](AddressRequestInfo *Info, int Error, struct addrinfo *AddressInfo)
						{
							auto Free1 = std::unique_ptr<AddressRequestInfo>(Info);
							auto Free2 = std::unique_ptr<struct addrinfo, decltype(&uv_freeaddrinfo)>(AddressInfo, &uv_freeaddrinfo);
							auto Free3 = std::unique_ptr<std::string>(PortString);
							auto Free4 = std::unique_ptr<std::string>(HostString);

							if (Error)
							{
								if (This->LogCallback)
									This->LogCallback(Local("Failed to look up host ^0: ^1", Directive.Host, uv_strerror(Error)));
								return;
							}

							auto Watcher = new uv_tcp_t;
							uv_tcp_init(uv_default_loop(), Watcher);

							using ConnectRequestInfo = UVData<uv_connect_t, int>;
							auto ConnectRequest = new ConnectRequestInfo{ [=, &This](ConnectRequestInfo *Info, int Error)
							{
								auto Free1 = std::unique_ptr<ConnectRequestInfo>(Info);
								if (Error)
								{
									uv_close(reinterpret_cast<uv_handle_t *>(Watcher), [](uv_handle_t *Watcher) { delete reinterpret_cast<uv_tcp_t *>(Watcher); });
									if (This->LogCallback)
										This->LogCallback(Local("Failed to connect to (^0:^1): ^2", Directive.Host, Directive.Port, uv_strerror(Error)));
									return;
								}

								ConnectionType *Socket = CreateConnection(Directive.Host, Directive.Port, Watcher, ReadCallback);

								std::lock_guard<std::mutex> Lock(This->Mutex);
								CleanConnections();
								This->Connections.emplace_back(Socket);
							}};
							uv_tcp_connect(ConnectRequest, Watcher, AddressInfo->ai_addr,
								[](uv_connect_t *Info, int Error)
									{ ConnectRequestInfo::Fix(static_cast<ConnectRequestInfo *>(Info), Error); });
						}};
						uv_getaddrinfo(uv_default_loop(), AddressRequest,
							[](uv_getaddrinfo_t *Info, int Error, struct addrinfo *AddressInfo)
								{ AddressRequestInfo::Fix(static_cast<AddressRequestInfo *>(Info), Error, AddressInfo); },
							HostString->c_str(), PortString->c_str(), nullptr);
					}
				}
			});
			uv_async_init(uv_default_loop(), AsyncOpenData, UVWatcherData<uv_async_t>::PreCallback);
			This->NotifyOpen = [&AsyncOpenData](void) { uv_async_send(AsyncOpenData); };

			auto AsyncTransferData = new UVWatcherData<uv_async_t>([&](UVWatcherData<uv_async_t> *)
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
			uv_async_init(uv_default_loop(), AsyncTransferData, UVWatcherData<uv_async_t>::PreCallback);
			This->NotifyTransfer = [&](void) { uv_async_send(AsyncTransferData); };

			auto AsyncScheduleData = new UVWatcherData<uv_async_t>([&](UVWatcherData<uv_async_t> *)
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

					auto TimerData = new UVWatcherData<uv_timer_t>([&, Directive](UVWatcherData<uv_timer_t> *TimerData)
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
					uv_timer_init(uv_default_loop(), TimerData);
					TimerCallbacks.emplace_back(TimerData, TimerCallbackFree);
					uv_timer_start(TimerData, UVWatcherData<uv_timer_t>::PreCallback, Directive.Seconds * 1000, 0);
				}
			});
			uv_async_init(uv_default_loop(), AsyncScheduleData, UVWatcherData<uv_async_t>::PreCallback);
			This->NotifySchedule = [&](void) { uv_async_send(AsyncScheduleData); };

			if (TimerPeriod)
			{
				auto TimerData = new UVWatcherData<uv_timer_t>([&](UVWatcherData<uv_timer_t> *Timer)
				{
					auto Now = GetNow();
					for (auto &Connection : This->Connections) Connection->HandleTimer(Now);
					uv_timer_again(Timer);
				});
				uv_timer_init(uv_default_loop(), TimerData);
				TimerCallbacks.emplace_back(TimerData, TimerCallbackFree);
				uv_timer_start(TimerData, UVWatcherData<uv_timer_t>::PreCallback, *TimerPeriod * 1000, *TimerPeriod * 1000);
			}

			This->InitSignal.notify_all();

			Lock.unlock();

			uv_run(uv_default_loop(), UV_RUN_DEFAULT);

			Lock.lock();
			This->Connections.clear();
			TimerCallbacks.clear();
			uv_close(reinterpret_cast<uv_handle_t *>(AsyncOpenData), [](uv_handle_t *Data) { delete reinterpret_cast<UVWatcherData<uv_async_t> *>(Data); });
			uv_close(reinterpret_cast<uv_handle_t *>(AsyncTransferData), [](uv_handle_t *Data) { delete reinterpret_cast<UVWatcherData<uv_async_t> *>(Data); });
			uv_close(reinterpret_cast<uv_handle_t *>(AsyncScheduleData), [](uv_handle_t *Data) { delete reinterpret_cast<UVWatcherData<uv_async_t> *>(Data); });

			uv_run(uv_default_loop(), UV_RUN_NOWAIT);
		}
};

#endif
