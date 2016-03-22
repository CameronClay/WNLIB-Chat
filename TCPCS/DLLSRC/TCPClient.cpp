#include "StdAfx.h"
#include "TCPClient.h"
#include "CNLIB/HeapAlloc.h"
#include "CNLIB/BufSize.h"
#include "CNLIB/File.h"
#include "CNLIB/MsgHeader.h"

TCPClientInterface* CreateClient(cfunc msgHandler, dcfunc disconFunc, DWORD nThreads, DWORD nConcThreads, UINT maxSendOps, UINT maxDataSize, UINT olCount, UINT sendBuffCount, UINT sendCompBuffCount, UINT sendMsgBuffCount, UINT maxCon, int compression, int compressionCO, float keepAliveInterval, SocketOptions sockOpts, void* obj)
{
	return construct<TCPClient>(msgHandler, disconFunc, nThreads, nConcThreads, maxSendOps, maxDataSize, olCount, sendBuffCount, sendMsgBuffCount, sendMsgBuffCount, maxCon, compression, compressionCO, keepAliveInterval, sockOpts, obj);
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
						key->CleanupRecvData();
						//key->Shutdown();
						continue;
					}
					//key->RecvDataCR(bytesTrans, ol);
					if (!key->RecvDataCR(bytesTrans))
						key->CleanupRecvData();
				}
				break;
			case OpType::sendsingle:
				key->SendDataCR((OverlappedSendSingle*)ol);
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
					key->SendDataCR((OverlappedSendSingle*)ol);
				}
				else if (ol->opType == OpType::recv)
				{
					key->CleanupRecvData();
					//key->Shutdown();
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


bool TCPClient::RecvDataCR(DWORD bytesTrans)
{
	return recvHandler.RecvDataCR(host, bytesTrans, GetBufferOptions(), nullptr);
}

void TCPClient::OnNotify(char* data, DWORD nBytes, void*)
{
	(function)(*this, MsgStreamReader{ data, nBytes - MSG_OFFSET });
}

void TCPClient::SendDataCR(OverlappedSendSingle* ol)
{
	FreeSendOl(ol);

	//If there are per client operations pending then attempt to complete them
	if (!opsPending.empty())
		SendServData(opsPending.front(), true);
}

void TCPClient::CleanupRecvData()
{
	host.Disconnect();

	RunDisconFunc();

	if (--opCounter == 0)
		SetEvent(shutdownEv);
}

void TCPClient::FreeSendOl(OverlappedSendSingle* ol)
{
	bufSendAlloc.FreeBuff(ol->sendBuff);
	olPool.dealloc(ol);

	if (--opCounter == 0)
		SetEvent(shutdownEv);
}

TCPClient::TCPClient(cfunc func, dcfunc disconFunc, DWORD nThreads, DWORD nConcThreads, UINT maxSendOps, UINT maxDataSize, UINT olCount, UINT sendBuffCount, UINT sendCompBuffCount, UINT sendMsgBuffCount, UINT maxCon, int compression, int compressionCO, float keepAliveInterval, SocketOptions sockOpts, void* obj)
	:
	function(func),
	disconFunc(disconFunc),
	iocp(nThreads, nConcThreads, IOCPThread),
	maxSendOps(maxSendOps),
	unexpectedShutdown(true),
	keepAliveInterval(keepAliveInterval),
	keepAliveHandler(nullptr),
	bufSendAlloc(maxDataSize, sendBuffCount, sendCompBuffCount, sendMsgBuffCount, compression, compressionCO),
	recvHandler(GetBufferOptions(), 2, this),
	olPool(sizeof(OverlappedSendSingle), maxSendOps),
	shutdownEv(NULL),
	sockOpts(sockOpts),
	obj(obj)
{

}

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
	opCounter(client.opCounter.load()),
	shutdownEv(client.shutdownEv),
	sockOpts(client.sockOpts),
	obj(client.obj)
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
		(void(*)(bool))disconFunc = client.disconFunc;
		iocp = std::move(client.iocp);
		const_cast<UINT&>(maxSendOps) = client.maxSendOps;
		unexpectedShutdown = client.unexpectedShutdown;
		keepAliveInterval = client.keepAliveInterval;
		keepAliveHandler = std::move(client.keepAliveHandler);
		bufSendAlloc = std::move(client.bufSendAlloc);
		recvHandler = std::move(client.recvHandler);
		olPool = std::move(client.olPool);
		opsPending = std::move(client.opsPending);
		opCounter = client.opCounter.load();
		shutdownEv = client.shutdownEv;
		sockOpts = client.sockOpts;
		obj = client.obj;

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

	return host.Connect(dest, port, ipv6, timeOut);
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
		//Stop sending keepalive packets
		destruct(keepAliveHandler);

		//Cancel all outstanding operations
		Disconnect();

		//Wait for all operations to cease
		WaitForSingleObject(shutdownEv, INFINITE);

		//Post a close message to all iocp threads
		iocp.Post(0, nullptr);

		//Wait for iocp threads to close, then cleanup iocp
		iocp.WaitAndCleanup();

		////Free up client resources
		//dealloc(recvBuff.head);

		CloseHandle(shutdownEv);
		shutdownEv = NULL;
	}
}

BuffSendInfo TCPClient::GetSendBuffer(DWORD hiByteEstimate, CompressionType compType)
{
	return bufSendAlloc.GetSendBuffer(hiByteEstimate);
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

bool TCPClient::SendServData(const BuffSendInfo& buffSendInfo, DWORD nBytes, BuffAllocator* alloc)
{
	return SendServData(buffSendInfo, nBytes, false, alloc);
}
bool TCPClient::SendServData(MsgStreamWriter streamWriter, BuffAllocator* alloc)
{
	return SendServData(BuffSendInfo{ streamWriter.GetData(), streamWriter.GetCompType() }, streamWriter.GetSize(), false, alloc);
}

bool TCPClient::SendServData(const BuffSendInfo& buffSendInfo, DWORD nBytes, bool msg, BuffAllocator* alloc)
{
	return SendServData(olPool.construct<OverlappedSendSingle>(bufSendAlloc.CreateBuff(buffSendInfo, nBytes, msg, -1, alloc)), false);
}
bool TCPClient::SendServData(OverlappedSendSingle* ol, bool popQueue)
{
	if (host.IsConnected())
	{
		if (opCounter.load() - 1 < maxSendOps)
		{
			++opCounter;

			long res = host.SendDataOl(&ol->sendBuff, ol);
			long err = WSAGetLastError();
			if ((res == SOCKET_ERROR) && (err != WSA_IO_PENDING))
			{
				FreeSendOl(ol);
			}
			else if (popQueue)
			{
				opsPending.pop();
			}
		}
		else
		{
			//queue the send for later
			opsPending.emplace(ol);
		}
	}
	else
	{
		FreeSendOl(ol);

		return false;
	}

	return true;
}

void TCPClient::SendMsg(short type, short message)
{
	short msg[] = { type, message };

	SendServData({ (char*)msg, NOCOMPRESSION }, MSG_OFFSET, true);
}
void TCPClient::SendMsg(const std::tstring& name, short type, short message)
{
	MsgStreamWriter streamWriter = CreateOutStream(StreamWriter::SizeType(name), type, message);
	streamWriter.Write(name);
	SendServData(streamWriter);
}

bool TCPClient::RecvServData()
{
	if (!host.IsConnected())
		return false;

	if (sockOpts.NoDelay())
		host.SetNoDelay(true);
	if (sockOpts.UseOwnBuf())
		host.SetTCPSendStack();

	if (!iocp.LinkHandle((HANDLE)host.GetSocket(), this))
		return false;

	++opCounter; //For recv

	recvHandler.StartRead(host);

	shutdownEv = CreateEvent(NULL, TRUE, FALSE, NULL);

	keepAliveHandler = construct<KeepAliveHandler>(this);

	if (!keepAliveHandler)
		return false;

	keepAliveHandler->SetKeepAliveTimer(keepAliveInterval);

	return true;
}

void TCPClient::KeepAlive()
{
	host.SendData(nullptr, 0);
}

void TCPClient::SetFunction(cfunc function)
{
	this->function = function;
}

void TCPClient::RunDisconFunc()
{
	disconFunc(unexpectedShutdown);
}

void TCPClient::SetShutdownReason(bool unexpected)
{
	unexpectedShutdown = unexpected;
}

cfuncP TCPClient::GetFunction()
{
	return &function;
}

Socket TCPClient::GetHost() const
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
	return opCounter.load();
}
UINT TCPClient::GetMaxSendOps() const
{
	return maxSendOps;
}

const SocketOptions TCPClient::GetSockOpts() const
{
	return sockOpts;
}