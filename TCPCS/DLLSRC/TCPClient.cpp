#include "StdAfx.h"
#include "TCPClient.h"
#include "CNLIB/HeapAlloc.h"
#include "CNLIB/BufSize.h"
#include "CNLIB/File.h"
#include "CNLIB/MsgHeader.h"

TCPClientInterface* CreateClient(cfunc msgHandler, dcfunc disconFunc, UINT maxSendOps, const BufferOptions& buffOpts, const SocketOptions& sockOpts, UINT olCount, UINT sendBuffCount, UINT sendCompBuffCount, UINT sendMsgBuffCount, float keepAliveInterval, void* obj)
{
	return construct<TCPClient>(msgHandler, disconFunc, maxSendOps, buffOpts, sockOpts, olCount, sendBuffCount, sendCompBuffCount, sendMsgBuffCount, keepAliveInterval, obj);
}

void DestroyClient(TCPClientInterface*& client)
{
	destruct((TCPClient*&)client);
}

static DWORD CALLBACK IOCPThread(LPVOID info)
{
	IOCP& iocp = *(IOCP*)info;
	DWORD bytesTrans = 0;
	OverlappedExt* ol = nullptr;
	TCPClient* key = NULL;

	bool res = false;
	do
	{
		res = GetQueuedCompletionStatus(iocp.GetHandle(), &bytesTrans, (PULONG_PTR)&key, (OVERLAPPED**)&ol, INFINITE);
		if (res)
		{
			if (!ol)
				break;
			switch (ol->opType)
			{
			case OpType::recv:
				{
					if (bytesTrans == 0)
					{
						key->CleanupRecv();
						continue;
					}

					key->RecvDataCR(key->RecvDataCR(bytesTrans));
				}
				break;
			case OpType::sendsingle:
				key->SendDataCR((OverlappedSendSingle*)ol, true);
				break;
			}
		}
		else
		{
			if (ol && key)
			{
				//temporary fix
				res = true;
				if (ol->opType == OpType::sendsingle)
				{
					key->SendDataCR((OverlappedSendSingle*)ol, true);
				}
				else if (ol->opType == OpType::recv)
				{
					key->CleanupRecv();
				}
			}
			//else
			//{
			//	//Error
			//}
		}
	} while (res);

	return 0;
}


RecvHandler::ReadError TCPClient::RecvDataCR(DWORD bytesTrans)
{
	return recvHandler.RecvDataCR(host, bytesTrans, GetBufferOptions(), nullptr);
}

void TCPClient::OnNotify(char* data, DWORD nBytes, void*)
{
    //problem here on disconnect
	(function)(*this, MsgStreamReader{ data, nBytes - MSG_OFFSET });
}

void TCPClient::SendDataCR(OverlappedSendSingle* ol, bool sendQueued)
{
	FreeSendOl(ol);
	DecOpCount();

	//If there are per client operations pending then attempt to complete them
	if (sendQueued && !opsPending.empty())
	{
		queueLock.Lock();
		if (!opsPending.empty())
			SendServData(opsPending.front(), true);
		queueLock.Unlock();
	}
}
void TCPClient::RecvDataCR(const ReadError error)
{
	if (error == ReadError::ReadFailed)
	{
		host.Disconnect();
		DecOpCount();
	}
	else if (error == ReadError::UserError)
		host.Disconnect();
}
void TCPClient::CleanupRecv()
{
	DecOpCount();
}
void TCPClient::FreePendingOps()
{
	if (!opsPending.empty())
	{
		queueLock.Lock();

		for (int i = 0, size = opsPending.size(); i < size; i++)
		{
			FreeSendOl(opsPending.front());
			opsPending.pop();
		}

		queueLock.Unlock();
	}
}

void TCPClient::CleanupRecvData()
{
	host.Disconnect();

	RunDisconFunc();

	//causes race conditions on dtor of keepalive
	//Stop sending keepalive packets
	//destruct(keepAliveHandler);
}

void TCPClient::FreeSendOl(OverlappedSendSingle* ol)
{
	bufSendAlloc.FreeBuff(ol->sendBuff);
	olPool.dealloc(ol);
}

TCPClient::TCPClient(cfunc msgHandler, dcfunc disconFunc, UINT maxSendOps, const BufferOptions& buffOpts, const SocketOptions& sockOpts, UINT olCount, UINT sendBuffCount, UINT sendCompBuffCount, UINT sendMsgBuffCount, float keepAliveInterval, void* obj)
	:
	function(msgHandler),
	disconFunc(disconFunc),
	iocp(nullptr),
	maxSendOps(maxSendOps + 1),//+1 for recv
	unexpectedShutdown(true),
	keepAliveInterval(keepAliveInterval),
	keepAliveHandler(nullptr),
	bufSendAlloc(buffOpts, sendBuffCount, sendCompBuffCount, sendMsgBuffCount),
	recvHandler(GetBufferOptions(), 2, this),
	olPool(sizeof(OverlappedSendSingle), maxSendOps),
	opCounter(0),
	shutdownEv(NULL),
	sockOpts(sockOpts),
	obj(obj),
	shuttingDown(false)
{}
TCPClient::TCPClient(TCPClient&& client)
	:
	host(client.host),
	function(client.function),
	disconFunc(client.disconFunc),
	iocp(std::move(client.iocp)),
	maxSendOps(client.maxSendOps),
	unexpectedShutdown(client.unexpectedShutdown),
	keepAliveInterval(client.keepAliveInterval),
	keepAliveHandler(std::move(client.keepAliveHandler)),
	bufSendAlloc(std::move(client.bufSendAlloc)),
	recvHandler(std::move(client.recvHandler)),
	olPool(std::move(client.olPool)),
	opsPending(std::move(client.opsPending)),
	queueLock(std::move(client.queueLock)),
	opCounter(client.GetOpCount()),
	shutdownEv(client.shutdownEv),
	sockOpts(client.sockOpts),
	obj(client.obj),
	shuttingDown(client.ShuttingDown())
{
	ZeroMemory(&client, sizeof(TCPClient));
}
TCPClient& TCPClient::operator=(TCPClient&& client)
{
	if(this != &client)
	{
		this->~TCPClient();

		host = client.host;
		function = client.function;
		*(void(**)(TCPClientInterface&, bool))&disconFunc = client.disconFunc;
		iocp = std::move(client.iocp);
		const_cast<UINT&>(maxSendOps) = client.maxSendOps;
		unexpectedShutdown = client.unexpectedShutdown;
		keepAliveInterval = client.keepAliveInterval;
		keepAliveHandler = std::move(client.keepAliveHandler);
		bufSendAlloc = std::move(client.bufSendAlloc);
		recvHandler = std::move(client.recvHandler);
		olPool = std::move(client.olPool);
		opsPending = std::move(client.opsPending);
		queueLock = std::move(client.queueLock);
		opCounter = client.GetOpCount();
		shutdownEv = client.shutdownEv;
		sockOpts = client.sockOpts;
		obj = client.obj;
		shuttingDown = client.ShuttingDown();

		ZeroMemory(&client, sizeof(TCPClient));
	}
	return *this;
}

TCPClient::~TCPClient()
{
	Shutdown();
}

bool TCPClient::Connect(const LIB_TCHAR* dest, const LIB_TCHAR* port, bool ipv6, float timeOut)
{
	if(host.IsConnected())
		return false;

	//reset so it will send correct message
	SetShutdownReason(true);

	return host.Connect(dest, port, ipv6, sockOpts.TCPSendSize(), sockOpts.TCPRecvSize(), sockOpts.NoDelay(), timeOut);
}

void TCPClient::Disconnect()
{
    SetShutdownReason(false);
	host.Disconnect();
}

void TCPClient::Shutdown()
{
	if (shutdownEv)
	{
		shuttingDown = true;

		//Stop sending keepalive packets
		destruct(keepAliveHandler);

		//Cancel all outstanding operations
		Disconnect();

		//Free all pending operations
		FreePendingOps();

		//Wait for all operations to cease
		WaitForSingleObject(shutdownEv, INFINITE);

		//Post a close message to all iocp threads
		iocp->Post(0, nullptr);

		//Wait for iocp threads to close, then cleanup iocp
		iocp->WaitAndCleanup();

		destruct(iocp);

		////Free up client resources
		//dealloc(recvBuff.head);

		CloseHandle(shutdownEv);
		shutdownEv = NULL;

		shuttingDown = false;
	}
}

BuffSendInfo TCPClient::GetSendBuffer(DWORD hiByteEstimate, CompressionType compType)
{
	return bufSendAlloc.GetSendBuffer(hiByteEstimate, compType);
}
BuffSendInfo TCPClient::GetSendBuffer(BuffAllocator* alloc, DWORD nBytes, CompressionType compType)
{
	return bufSendAlloc.GetSendBuffer(alloc, nBytes, compType);
}

MsgStreamWriter TCPClient::CreateOutStream(DWORD hiByteEstimate, short type, short msg, CompressionType compType)
{
	return bufSendAlloc.CreateOutStream(hiByteEstimate, type, msg, compType);
}
MsgStreamWriter TCPClient::CreateOutStream(BuffAllocator* alloc, DWORD nBytes, short type, short msg, CompressionType compType)
{
	return bufSendAlloc.CreateOutStream(alloc, nBytes, type, msg, compType);
}


const BufferOptions TCPClient::GetBufferOptions() const
{
	return bufSendAlloc.GetBufferOptions();
}

bool TCPClient::SendServData(const BuffSendInfo& buffSendInfo, DWORD nBytes)
{
	return SendServData(buffSendInfo, nBytes, false);
}
bool TCPClient::SendServData(const MsgStreamWriter& streamWriter)
{
	return SendServData(streamWriter.GetBuffSendInfo(), streamWriter.GetSize(), false);
}

bool TCPClient::SendServData(const BuffSendInfo& buffSendInfo, DWORD nBytes, bool msg)
{
	return SendServData(bufSendAlloc.CreateBuff(buffSendInfo, nBytes, msg), false);
}
bool TCPClient::SendServData(const WSABufSend& sendBuff, bool popQueue)
{
	return SendServData(olPool.construct<OverlappedSendSingle>(sendBuff), false);
}
bool TCPClient::SendServData(OverlappedSendSingle* ol, bool popQueue)
{
	if (ShuttingDown())
	{
		FreeSendOl(ol);
		return false;
	}

	//opCount check needed incase client is already being removed
	const UINT opCount = GetOpCount();
	if (host.IsConnected() && opCount)
	{
		if ((opCount < maxSendOps) && (popQueue || opsPending.empty()))
		{
			++opCounter;

			long res = host.SendDataOl(&ol->sendBuff, ol);
			long err = WSAGetLastError();
			if ((res == SOCKET_ERROR) && (err != WSA_IO_PENDING))
				SendDataCR(ol, false);
			else if (popQueue)
				opsPending.pop();
		}
		else if (!popQueue)
		{
			//okay to lock and unlock here
			queueLock.Lock();

			//queue the send for later
			opsPending.push(ol);

			queueLock.Unlock();
		}
	}
	else
	{
		FreeSendOl(ol);
		return false;
	}

	return true;
}

void TCPClient::DecOpCount()
{
	if (--opCounter == 0)
	{
		CleanupRecvData();
		SetEvent(shutdownEv);
	}
}

void TCPClient::SendMsg(short type, short message)
{
	short msg[] = { type, message };

	SendServData({ (char*)msg, NOCOMPRESSION, 0 }, MSG_OFFSET, true);
}
void TCPClient::SendMsg(const std::tstring& name, short type, short message)
{
	MsgStreamWriter streamWriter = CreateOutStream(StreamWriter::SizeType(name), type, message);
	streamWriter.Write(name);
	SendServData(streamWriter);
}

bool TCPClient::RecvServData(DWORD nThreads, DWORD nConcThreads)
{
	if (!host.IsConnected())
		return false;

	//client can disconnect and iocp wont be cleaned up due to nonblocking disconnect
	if (!iocp)
		iocp = construct<IOCP>(nThreads, nConcThreads, IOCPThread);

	if (!iocp->LinkHandle((HANDLE)host.GetSocket(), this))
		return false;

	if (!shutdownEv)
		shutdownEv = CreateEvent(NULL, TRUE, FALSE, NULL);
	else
		ResetEvent(shutdownEv);

	++opCounter; //For recv
	if (!recvHandler.StartRecv(host))
		DecOpCount();

	if (keepAliveInterval != 0.0f)
	{
		keepAliveHandler = construct<KeepAliveHandler>(this);

		if (!keepAliveHandler)
			return false;

		keepAliveHandler->SetKeepAliveTimer(keepAliveInterval);
	}

	return true;
}

void TCPClient::KeepAlive()
{
	SendMsg(TYPE_KEEPALIVE, MSG_KEEPALIVE);
	//host.SendData(nullptr, 0);
}

void TCPClient::RunDisconFunc()
{
    //problem here
	disconFunc(*this, unexpectedShutdown);
}

void TCPClient::SetShutdownReason(bool unexpected)
{
	unexpectedShutdown = unexpected;
}

void TCPClient::SetFunction(cfunc function)
{
	this->function = function;
}
cfuncP TCPClient::GetFunction()
{
	return &function;
}

const Socket TCPClient::GetHost() const
{
	return host;
}
void* TCPClient::GetObj() const
{
	return obj;
}
dcfunc TCPClient::GetDisfunc() const
{
	return disconFunc;
}

void TCPClient::SetKeepAliveInterval(float interval)
{
	keepAliveInterval = interval;
	keepAliveHandler->SetKeepAliveTimer(keepAliveInterval);
}
float TCPClient::GetKeepAliveInterval() const
{
	return keepAliveInterval;
}

bool TCPClient::IsConnected() const
{
	return host.IsConnected();
}

UINT TCPClient::GetOpCount() const
{
	return opCounter.load(std::memory_order_acquire);
}
UINT TCPClient::GetMaxSendOps() const
{
	return maxSendOps;
}
bool TCPClient::ShuttingDown() const
{
	return shuttingDown.load(std::memory_order_acquire);
}

const SocketOptions TCPClient::GetSockOpts() const
{
	return sockOpts;
}
