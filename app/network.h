#ifndef network_h
#define network_h

#include "shared.h"
#include "type.h"
#include "translation/translation.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <ev.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <list>

template <typename ConnectionType> struct Network
{
	typedef std::function<ConnectionType *(std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop)> CreateConnectionCallback;

	struct Connection
	{
		Connection(std::string const &Host, uint16_t Port, int Socket, struct ev_loop *EVLoop, ConnectionType &DerivedThis) :
			Dead{false}, Host{Host}, Port{Port}, Socket{Socket}, EVLoop{EVLoop}, ReadBuffer{*this}, ReadWatcher{DerivedThis}, WriteWatcher{DerivedThis}
		{
			if (this->Socket < 0)
			{
				struct hostent *HostInfo = gethostbyname(Host.c_str());
				if (!HostInfo) throw ConstructionError() << Local("Failed to look up host (^0)", Host);
				this->Socket = socket(AF_INET, SOCK_STREAM, 0);
				if (this->Socket < 0) throw ConstructionError() << Local("Failed to open socket (^0:^1): ^2", Host, Port, strerror(errno));
				sockaddr_in AddressInfo{};
				AddressInfo.sin_family = AF_INET;
				memcpy(&AddressInfo.sin_addr, HostInfo->h_addr_list[0], static_cast<size_t>(HostInfo->h_length));
				AddressInfo.sin_port = htons(Port);

				if (connect(this->Socket, reinterpret_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1)
					throw ConstructionError() << Local("Failed to connect (^0:^1): ^2", Host, Port, strerror(errno));
			}

			ev_io_init(&ReadWatcher, InternalReadCallback, this->Socket, EV_READ);
			ev_io_start(EVLoop, &ReadWatcher);
			ev_io_init(&WriteWatcher, InternalWriteCallback, this->Socket, EV_WRITE);
			ev_io_start(EVLoop, &WriteWatcher);
		}

		~Connection(void) { if (!Dead) Die(); }

		// Network thread only
		bool IsDead(void) { return Dead; }
		uint64_t GetDiedAt(void) { assert(Dead); return DiedAt; }

		void WakeIdleWrite(void) { if (Dead) return; ev_io_start(EVLoop, &WriteWatcher); }

		void RawSend(std::vector<uint8_t> const &Data)
		{
			if (Dead) return;
			int Sent = write(Socket, &Data[0], Data.size());
			if (Sent == -1) if (errno == EPIPE) Die();
			// TODO do something if sent = -1 or != Data.size?
		}

		template <typename MessageType, typename... ArgumentTypes> void Send(MessageType, ArgumentTypes const &... Arguments)
		{
			if (Dead) return;
			auto const &Data = MessageType::Write(Arguments...);
			RawSend(Data);
		}

		void Die(void)
		{
			assert(!Dead);

			ev_io_stop(EVLoop, &ReadWatcher);
			ev_io_stop(EVLoop, &WriteWatcher);

			assert(Socket >= 0);
			close(Socket);

			Dead = true;
			DiedAt = GetNow();
		}

		private:
			friend struct Network<ConnectionType>;

			bool Dead;
			uint64_t DiedAt;

			std::string Host;
			uint16_t Port;
			int Socket;

			struct ev_loop *EVLoop;

			struct ReadBufferType
			{
				ReadBufferType(Connection &This) : This(This) {}

				Protocol::SubVector<uint8_t> Read(size_t Length, size_t Offset = 0)
				{
					assert(This.Socket >= 0);
					if (Length == 0) return {};
					if (Offset + Length <= Buffer.size()) return {Buffer, Offset, Length};
					auto const Difference = Offset + Length - Buffer.size();
					auto const OriginalLength = Buffer.size();
					Buffer.resize(Offset + Length);
					int Count = recv(This.Socket, &Buffer[OriginalLength], Difference, 0);
					if (Count <= 0)
					{
						This.Die();
						Buffer.resize(OriginalLength);
						return {};
					}
					if (Count < Difference) { Buffer.resize(OriginalLength + Count); return {}; }
					return {Buffer, Offset, Length};
				}

				void Consume(size_t Length)
				{
					assert(This.Socket >= 0);
					assert(Length > 0);
					assert(Length <= Buffer.size());
					Buffer.erase(Buffer.begin(), Buffer.begin() + Length);
				}

				Connection &This;
				std::vector<uint8_t> Buffer;
			} ReadBuffer;

			std::function<void(ConnectionType &Socket)> ReadCallback;

			struct Watcher : ev_io
			{
				Watcher(ConnectionType &This) : This(This) {}
				ConnectionType &This;
			} ReadWatcher, WriteWatcher;

			static void InternalReadCallback(struct ev_loop *, ev_io *Data, int EventFlags)
			{
				assert(EventFlags & EV_READ);
				auto &Watcher = *static_cast<Network<ConnectionType>::Connection::Watcher *>(Data);
				Watcher.This.ReadCallback(Watcher.This);
			}

			static void InternalWriteCallback(struct ev_loop *, ev_io *Data, int EventFlags)
			{
				assert(EventFlags & EV_WRITE);
				auto &Watcher = *static_cast<Network<ConnectionType>::Connection::Watcher *>(Data);
				if (!Watcher.This.IdleWrite())
					ev_io_stop(Watcher.This.EVLoop, Data);
			}
	};

	struct Listener
	{
		Listener(std::string const &Host, uint16_t Port) : Host{Host}, Port{Port}
		{
			Socket = socket(AF_INET, SOCK_STREAM, 0);
			if (Socket < 0) throw ConstructionError() << Local("Failed to open socket (^0:^1): ^2", Host, Port, strerror(errno));
			sockaddr_in AddressInfo{};
			AddressInfo.sin_family = AF_INET;
			AddressInfo.sin_addr.s_addr = inet_addr(Host.c_str());
			AddressInfo.sin_port = htons(Port);

			if (bind(Socket, reinterpret_cast<sockaddr *>(&AddressInfo), sizeof(AddressInfo)) == -1)
				throw ConstructionError() << Local("Failed to bind (^0:^1): ^2", Host, Port, strerror(errno));
			if (listen(Socket, 2) == -1) throw ConstructionError() << Local("Failed to listen on (^0): ^1", Port, strerror(errno));
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

	Optional<uint64_t> IdleSince(void)
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
		Optional<uint64_t> DeletedIdleSince;
		std::list<std::unique_ptr<ConnectionType>> Connections;

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
			std::list<std::unique_ptr<EVData<ev_io>>> IOCallbacks;
			std::list<std::unique_ptr<EVData<ev_timer>>> TimerCallbacks;

			// Set up intermediary event handlers and libev loop
			struct ev_loop *EVLoop = ev_default_loop(0);

			Protocol::Reader<MessageTypes...> Reader;

			std::list<std::unique_ptr<Listener>> Listeners;

			auto const ReadCallback = [&](ConnectionType &Socket)
			{
				bool Result = Reader.Read(Socket.ReadBuffer, Socket);
				if (!Result)
				{
					std::cout << "::REND:: failed to read." << std::endl;
				}
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

			EVData<ev_async> AsyncOpenData([&](EVData<ev_async> *)
			{
				/// Exit loop if dying
				if (This->Die) { ev_break(EVLoop); return; }

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
						catch (ConstructionError &Error) { if (This->LogCallback) This->LogCallback(Error); continue; }
						Listeners.emplace_back(Socket);

						auto ListenerData = new EVData<ev_io>([&, Socket](EVData<ev_io> *)
						{
							CleanConnections();
							ConnectionType *ConnectionInfo;
							try { ConnectionInfo = Socket->Accept(CreateConnection, EVLoop); }
							catch (ConstructionError &Error) { if (This->LogCallback) This->LogCallback(Local("Failed to accept connection on ^0:^1", Socket->Host, Socket->Port)); return; }
							ConnectionInfo->ReadCallback = ReadCallback;
							std::lock_guard<std::mutex> Lock(This->Mutex);
							This->Connections.push_back(std::unique_ptr<ConnectionType>{ConnectionInfo});
						});
						IOCallbacks.emplace_back(ListenerData);
						ev_io_init(ListenerData, EVData<ev_io>::PreCallback, Socket->Socket, EV_READ);
						ev_io_start(EVLoop, ListenerData);
					}
					else
					{
						CleanConnections();
						ConnectionType *Socket = nullptr;
						try { Socket = CreateConnection(Directive.Host, Directive.Port, -1, EVLoop); }
						catch (ConstructionError &Error) { if (This->LogCallback) This->LogCallback(Error); continue; }
						Socket->ReadCallback = ReadCallback;
						This->Connections.emplace_back(Socket);
					}
				}
			});
			ev_async_init(&AsyncOpenData, EVData<ev_async>::PreCallback);
			This->NotifyOpen = [&EVLoop, &AsyncOpenData](void) { ev_async_send(EVLoop, &AsyncOpenData); };
			ev_async_start(EVLoop, &AsyncOpenData);

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
			ev_async_start(EVLoop, &AsyncTransferData);

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
			ev_async_start(EVLoop, &AsyncScheduleData);

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

			This->Connections.clear();
		}
};

#endif
