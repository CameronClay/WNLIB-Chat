#include "TCPClient.h"
#include "HeapAlloc.h"
#include "Messages.h"
#include "File.h"
#include "MsgStream.h"

TCPClientInterface* CreateClient(cfunc msgHandler, dcfunc disconFunc, int compression, void* obj)
{
	return construct<TCPClient>(msgHandler, disconFunc, compression, obj);
}

void DestroyClient(TCPClientInterface*& client)
{
	destruct((TCPClient*&)client);
}



struct SendInfo
{
	SendInfo(TCPClient& client, char* data, const DWORD nBytes)
		:
		client(client),
		data(data),
		nBytes(nBytes)
	{}

	SendInfo(SendInfo&& info)
		:
		client(info.client),
		data(info.data),
		nBytes(info.nBytes)
	{
		ZeroMemory(&info, sizeof(SendInfo));
	}

	TCPClient& client;
	char* data;
	const DWORD nBytes;
};


TCPClient::TCPClient(cfunc func, dcfunc disconFunc, int compression, void* obj)
	:
	function(func),
	disconFunc(disconFunc),
	obj(obj),
	compression(compression),
	host(INVALID_SOCKET),
	recv(NULL),
	unexpectedShutdown(true)
{}

TCPClient::TCPClient(TCPClient&& client)
	:
	host(client.host),
	function(client.function),
	disconFunc(client.disconFunc),
	obj(client.obj),
	recv(client.recv),
	sendSect(client.sendSect),
	compression(client.compression),
	unexpectedShutdown(client.unexpectedShutdown)
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
		const_cast<void(*&)(bool unexpected)>(disconFunc) = client.disconFunc;
		obj = client.obj;
		recv = client.recv;
		sendSect = client.sendSect;
		const_cast<int&>(compression) = client.compression;

		ZeroMemory(&client, sizeof(TCPClient));
	}
	return *this;
}

TCPClient::~TCPClient()
{
	Shutdown();
}

static DWORD CALLBACK SendData(LPVOID param)
{
	SendInfo* data = (SendInfo*)param;
	TCPClient& client = data->client;
	Socket& pc = client.GetHost();
	CRITICAL_SECTION* sendSect = client.GetSendSect();

	EnterCriticalSection(sendSect);

	if (pc.SendData(&data->nBytes, sizeof(DWORD)) > 0)
	{
		DWORD nBytesComp = FileMisc::GetCompressedBufferSize(data->nBytes);
		BYTE* dataComp = alloc<BYTE>(nBytesComp);
		nBytesComp = FileMisc::Compress(dataComp, nBytesComp, (const BYTE*)data->data, data->nBytes, client.GetCompression());
		if (pc.SendData(&nBytesComp, sizeof(DWORD)) > 0)
		{
			if (pc.SendData(dataComp, nBytesComp) > 0)
			{
				dealloc(dataComp);
				destruct(data);
				LeaveCriticalSection(sendSect);
				return 0;
			}
			else dealloc(dataComp);
		}
		else dealloc(dataComp);
	}

	destruct(data);
	client.Disconnect();
	LeaveCriticalSection(sendSect);
	return 0;
}

static DWORD CALLBACK ReceiveData(LPVOID param)
{
	TCPClient& client = *(TCPClient*)param;
	DWORD nBytesComp = 0, nBytesDecomp = 0;
	Socket& host = client.GetHost();
	void* obj = client.GetObj();

	while (host.IsConnected())//break out if you disconnected from server(intentionaly)
	{
		if (host.ReadData(&nBytesDecomp, sizeof(DWORD)) > 0)
		{
			if (host.ReadData(&nBytesComp, sizeof(DWORD)) > 0)
			{
				BYTE* compBuffer = alloc<BYTE>(nBytesComp + nBytesDecomp);
				if (host.ReadData(compBuffer, nBytesComp) > 0)
				{
					BYTE* dest = &compBuffer[nBytesComp];
					FileMisc::Decompress(dest, nBytesDecomp, compBuffer, nBytesComp);
					(*client.GetFunction())(client, dest, nBytesDecomp, obj);
					dealloc(compBuffer);
				}
				else
				{
					dealloc(compBuffer);
					break;
				}
			}
			else
				break;
		}
		else
			break;
	}

	client.RunDisconFunc();

	//Cleanup
	client.Disconnect();
	DeleteCriticalSection(client.GetSendSect());
	client.CloseRecvHandle();

	return 0;
}

//IP or HostName for dest
bool TCPClient::Connect( const LIB_TCHAR* dest, const LIB_TCHAR* port, float timeOut )
{
	if(host.IsConnected())
		return false;

	//reset so it will send correct message
	SetShutdownReason(true);

	host.Connect( dest, port, timeOut );

	if(!host.IsConnected())
		return false;

	return true;
}

void TCPClient::Disconnect()
{
	SetShutdownReason(false);
	host.Disconnect(); //causes recvThread to close
}

void TCPClient::Shutdown()
{
	if(recv)
	{
		Disconnect();
		WaitForSingleObject(recv, INFINITE);//Handle closed in thread
	}
}

HANDLE TCPClient::SendServData(const char* data, DWORD nBytes)
{
	return CreateThread(NULL, 0, SendData, (LPVOID)construct<SendInfo>(*this, (char*)data, nBytes), NULL, NULL);
}

bool TCPClient::RecvServData()
{
	if(!host.IsConnected())
		return false;

	recv = CreateThread(NULL, 0, ReceiveData, this, NULL, NULL);
	InitializeCriticalSection(&sendSect);

	return true;
}

void TCPClient::SendMsg(char type, char message)
{
	char msg[] = { type, message };

	HANDLE hnd = SendServData(msg, MSG_OFFSET);
	WaitAndCloseHandle(hnd);
}

void TCPClient::SendMsg(const std::tstring& name, char type, char message)
{
	MsgStreamWriter streamWriter(type, message, (name.size() + 1) * sizeof(LIB_TCHAR));
	streamWriter.WriteEnd(name.c_str());
	HANDLE hnd = SendServData(streamWriter, streamWriter.GetSize());
	WaitAndCloseHandle(hnd);
}

void TCPClient::Ping()
{
	SendMsg(TYPE_PING, MSG_PING);
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

void TCPClient::CloseRecvHandle()
{
	if(recv)
	{
		CloseHandle(recv);
		recv = NULL;
	}
}

cfuncP TCPClient::GetFunction()
{
	return &function;
}

Socket& TCPClient::GetHost()
{
	return host;
}

void* TCPClient::GetObj() const
{
	return obj;
}

int TCPClient::GetCompression() const
{
	return compression;
}

CRITICAL_SECTION* TCPClient::GetSendSect()
{
	return &sendSect;
}

dcfunc TCPClient::GetDisfunc() const
{
	return disconFunc;
}

bool TCPClient::IsConnected() const
{
	return host.IsConnected();
}