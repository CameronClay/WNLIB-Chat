#include "TCPClient.h"
#include "HeapAlloc.h"
#include "Messages.h"
#include "File.h"

CRITICAL_SECTION cs_Send;

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


TCPClient::TCPClient(cfunc func, void(*const disconFunc)(), void* obj, int compression)
	:
	function(func),
	disconFunc(disconFunc),
	obj(obj),
	compression(compression),
	host(INVALID_SOCKET),
	recv(NULL)
{}

TCPClient::TCPClient(TCPClient&& client)
	:
	host(client.host),
	function(client.function),
	disconFunc(client.disconFunc),
	obj(client.obj),
	recv(client.recv),
	compression(client.compression)
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
		const_cast<void(*&)()>(disconFunc) = client.disconFunc;
		obj = client.obj;
		recv = client.recv;
		compression = client.compression;

		ZeroMemory(&client, sizeof(TCPClient));
	}
	return *this;
}

TCPClient::~TCPClient()
{
	Disconnect();
}

static DWORD CALLBACK SendData(LPVOID param)
{
	SendInfo* data = (SendInfo*)param;
	TCPClient& client = data->client;
	Socket& pc = client.GetHost();

	EnterCriticalSection(&cs_Send);

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
				LeaveCriticalSection(&cs_Send);
				return 0;
			}
			else dealloc(dataComp);
		}
		else dealloc(dataComp);
	}

	destruct(data);
	client.Disconnect();
	LeaveCriticalSection(&cs_Send);
	return 0;
}

static DWORD CALLBACK ReceiveData(LPVOID param)
{
	TCPClient& client = *(TCPClient*)param;
	DWORD nBytesComp = 0, nBytesDecomp = 0;
	Socket& host = client.GetHost();

	while (true)
	{
		if (host.ReadData(&nBytesDecomp, sizeof(DWORD)) > 0)
		{
			if (host.ReadData(&nBytesComp, sizeof(DWORD)) > 0)
			{
				BYTE* compBuffer = alloc<BYTE>(nBytesComp);
				if (host.ReadData(compBuffer, nBytesComp) > 0)
				{
					BYTE* deCompBuffer = alloc<BYTE>(nBytesDecomp);
					FileMisc::Decompress(deCompBuffer, nBytesDecomp, compBuffer, nBytesComp);
					dealloc(compBuffer);
					(*client.GetFunction())(param, deCompBuffer, nBytesDecomp, client.GetObj());
					dealloc(deCompBuffer);
				}
				else
				{
					dealloc(compBuffer);
					break;
				}
			}
			else break;
		}
		else break;
	}

	client.GetDisfunc()();
	client.Disconnect();

	return 0;
}

//IP or HostName for dest
void TCPClient::Connect( const TCHAR* dest, const TCHAR* port, float timeOut )
{
	host.Connect( dest, port, timeOut );
	InitializeCriticalSection(&cs_Send);
}

void TCPClient::Disconnect()
{
	host.Disconnect();
	if (recv)
	{
		TerminateThread(recv, 0);
		CloseHandle(recv);
		recv = NULL;
	}
	DeleteCriticalSection(&cs_Send);
}

HANDLE TCPClient::SendServData(const char* data, DWORD nBytes)
{
	return CreateThread(NULL, 0, SendData, (LPVOID)construct<SendInfo>(SendInfo(*this, (char*)data, nBytes)), NULL, NULL);
}

void TCPClient::RecvServData()
{
	recv = CreateThread(NULL, 0, ReceiveData, this, NULL, NULL);
}

void TCPClient::SendMsg(char type, char message)
{
	char msg[] = { type, message };

	HANDLE hnd = SendServData(msg, MSG_OFFSET);
	TCPClient::WaitAndCloseHandle(hnd);
}

void TCPClient::SendMsg(std::tstring& name, char type, char message)
{
	const DWORD nBytes = MSG_OFFSET + ((name.size() + 1) * sizeof(TCHAR));
	char* msg = alloc<char>(nBytes);
	msg[0] = type;
	msg[1] = message;
	memcpy(&msg[MSG_OFFSET], name.c_str(), nBytes - MSG_OFFSET);
	HANDLE hnd = SendServData(msg, nBytes);
	TCPClient::WaitAndCloseHandle(hnd);
	dealloc(msg);
}

void TCPClient::WaitAndCloseHandle(HANDLE& hnd)
{
	WaitForSingleObject(hnd, INFINITE);
	CloseHandle(hnd);
	hnd = INVALID_HANDLE_VALUE;
}

void TCPClient::Ping()
{
	SendMsg(TYPE_PING, MSG_PING);
}

void TCPClient::SetFunction(cfunc function)
{
	this->function = function;
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

void TCPClient::WaitForRecvThread() const
{
	WaitForSingleObject(recv, INFINITE);
}

int TCPClient::GetCompression() const
{
	return compression;
}

void(*TCPClient::GetDisfunc()) ()
{
	return disconFunc;
}

bool TCPClient::Connected() const
{
	return host.IsConnected();
}
