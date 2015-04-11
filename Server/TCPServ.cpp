#include "TCPServ.h"
#include "HeapAlloc.h"
#include "File.h"
#include "Messages.h"
#include "Format.h"

CRITICAL_SECTION clientSect, sendSect;

struct Data
{
	Data(TCPServ& serv, USHORT pos)
		:
		serv(serv),
		pos(pos)
	{}
	Data(Data&& data)
		:
		serv(data.serv),
		pos(data.pos)
	{
		ZeroMemory(&data, sizeof(Data));
	}

	TCPServ& serv;
	USHORT pos;
};

struct SAData
{
	SAData(TCPServ& serv, char* data, DWORD nBytesDecomp, Socket exAddr, const bool single)
		:
		serv(serv),
		nBytesComp(0),
		data(data),
		nBytesDecomp(nBytesDecomp),
		exAddr(exAddr),
		single(single)
	{}
	SAData(SAData&& data)
		:
		serv(data.serv),
		nBytesComp(data.nBytesComp),
		data(data.data),
		nBytesDecomp(data.nBytesDecomp),
		exAddr(data.exAddr),
		single(data.single)
	{
		ZeroMemory(&data, sizeof(SAData));
	}

	TCPServ& serv;
	DWORD nBytesComp, nBytesDecomp;
	char* data;
	Socket exAddr;
	bool single;
};

struct SADataEx
{
	SADataEx(TCPServ& serv, char* data, DWORD nBytesDecomp, std::vector<Socket>& pcs)
		:
		serv(serv),
		nBytesComp(0),
		data(data),
		nBytesDecomp(nBytesDecomp),
		pcs(pcs)
	{}
	SADataEx(SADataEx&& data)
		:
		serv(data.serv),
		nBytesComp(data.nBytesComp),
		data(data.data),
		nBytesDecomp(data.nBytesDecomp),
		pcs(data.pcs)
	{
		ZeroMemory(&data, sizeof(SADataEx));
	}

	TCPServ& serv;
	DWORD nBytesComp, nBytesDecomp;
	char* data;
	std::vector<Socket> pcs;
};

struct SData
{
	SData(SAData* saData, Socket pc)
		:
		saData(saData),
		pc(pc)
	{}
	SData(SData&& data)
		:
		saData(data.saData),
		pc(data.pc)
	{
		ZeroMemory(&data, sizeof(SData));
	}
	SAData* const saData;
	Socket pc;
};

struct SDataEx
{
	SDataEx(SADataEx* saData, Socket pc)
		:
		saData(saData),
		pc(pc)
	{}
	SDataEx(SDataEx&& data)
		:
		saData(data.saData),
		pc(data.pc)
	{
		ZeroMemory(&data, sizeof(SDataEx));
	}
	SADataEx* const saData;
	Socket pc;
};


DWORD CALLBACK WaitForConnections( LPVOID tcpServ )
{
	TCPServ& serv = *(TCPServ*)tcpServ;
	while( true )
	{
		Socket temp = serv.GetHost().AcceptConnection();
		if( temp != INVALID_SOCKET )
		{
			if(!serv.MaxClients())
			{
				serv.AddClient(temp);
			}
			else
			{
				char msg[] = { TYPE_CHANGE, MSG_SERVERFULL };
				HANDLE hnd = serv.SendClientData(msg, MSG_OFFSET, temp, true);
				TCPServ::WaitAndCloseHandle(hnd);

				temp.Disconnect();
			}
		}
	}
	return 0;
}


static DWORD CALLBACK RecvData(LPVOID info)
{
	Data* data = (Data*)info;
	TCPServ& serv = data->serv;
	void* obj = serv.GetObj();
	auto& vect = serv.GetClients();
	USHORT& pos = data->pos;
	Socket pc = vect[pos].pc;
	DWORD nBytesComp = 0, nBytesDecomp = 0;

	while (true)
	{
		if (pc.ReadData(&nBytesDecomp, sizeof(DWORD)) > 0)
		{
			if (pc.ReadData(&nBytesComp, sizeof(DWORD)) > 0)
			{
				BYTE* compBuffer = alloc<BYTE>(nBytesComp);
				if (pc.ReadData(compBuffer, nBytesComp) > 0)
				{
					BYTE* deCompBuffer = alloc<BYTE>(nBytesDecomp); //Must dealloc in vect[pos].func
					FileMisc::Decompress(deCompBuffer, nBytesDecomp, compBuffer, nBytesComp);
					dealloc(compBuffer);

					vect[pos].pingHandler.SetInactivityTimer(serv, pc, INACTIVETIME, PINGTIME);
					(vect[pos].func)(&serv, pos, deCompBuffer, nBytesDecomp, obj);
				}
				else
				{
					//PrintError(GetLastError());
					dealloc(compBuffer);
					break;
				}
			}
			else break;
		}
		else break;
	}

	serv.RemoveClient(pos);
	destruct(data);
	return 0;
}


static DWORD CALLBACK SendData( LPVOID info )
{
	SData* data = (SData*)info;
	SAData* saData = data->saData;
	TCPServ& serv = saData->serv;
	auto& vect = serv.GetClients();
	Socket pc = data->pc;

	if (pc.SendData(&saData->nBytesDecomp, sizeof(DWORD)) > 0)
	{
		if (pc.SendData(&saData->nBytesComp, sizeof(DWORD)) > 0)
		{
			if (pc.SendData(saData->data, saData->nBytesComp) > 0)
			{
				destruct(data);
				return 0;
			}
		}
	}

	//No need to remove client, that is handled in recv thread
	//PrintError(GetLastError());
	destruct(data);

	return 0;
}

static DWORD CALLBACK SendDataEx(LPVOID info)
{
	SDataEx* data = (SDataEx*)info;
	SADataEx* saData = data->saData;
	TCPServ& serv = saData->serv;
	auto& vect = serv.GetClients();
	Socket pc = data->pc;

	if (pc.SendData(&saData->nBytesDecomp, sizeof(DWORD)) > 0)
	{
		if (pc.SendData(&saData->nBytesComp, sizeof(DWORD)) > 0)
		{
			if (pc.SendData(saData->data, saData->nBytesComp) > 0)
			{
				destruct(data);
				return 0;
			}
		}
	}

	//No need to remove client, that is handled in recv thread
	//PrintError(GetLastError());
	destruct(data);

	return 0;
}


static DWORD CALLBACK SendAllData( LPVOID info )
{
	SAData* data = (SAData*)info;
	auto& clients = data->serv.GetClients();
	std::vector<HANDLE> hnds;
	void* dataDeComp = data->data;

	const DWORD nBytesComp = FileMisc::GetCompressedBufferSize(data->nBytesDecomp);
	BYTE* dataComp = alloc<BYTE>(nBytesComp);
	data->nBytesComp = FileMisc::Compress(dataComp, nBytesComp, (const BYTE*)dataDeComp, data->nBytesDecomp, data->serv.GetCompression());
	data->data = (char*)dataComp;

	EnterCriticalSection(&sendSect);

	if(data->single)
	{
		if(data->exAddr.IsConnected())
		{
			hnds.push_back(CreateThread(NULL, 0, SendData, construct<SData>(SData(data, data->exAddr)), CREATE_SUSPENDED, NULL));
		}
	}
	else
	{
		for(USHORT i = 0; i < clients.size(); i++)
		{
			if((data->exAddr.IsConnected() && clients[i].pc != data->exAddr) || (!data->exAddr.IsConnected()))
				hnds.push_back(CreateThread(NULL, 0, SendData, construct<SData>(SData(data, clients[i].pc)), CREATE_SUSPENDED, NULL));
		}
	}

	for (HANDLE& h : hnds)
		ResumeThread(h);

	WaitForMultipleObjects(hnds.size(), hnds.data(), true, INFINITE);

	for(HANDLE& h : hnds)
		CloseHandle(h);

	LeaveCriticalSection(&sendSect);


	hnds.clear();
	dealloc(dataComp);
	destruct(data);

	return 0;
}

static DWORD CALLBACK SendAllDataEx( LPVOID info )
{
	SADataEx* data = (SADataEx*)info;
	auto& pcs = data->pcs;
	std::vector<HANDLE> hnds;
	void* dataDeComp = data->data;

	const DWORD nBytesComp = FileMisc::GetCompressedBufferSize(data->nBytesDecomp);
	BYTE* dataComp = alloc<BYTE>(nBytesComp);
	data->nBytesComp = FileMisc::Compress(dataComp, nBytesComp, (const BYTE*)dataDeComp, data->nBytesDecomp, data->serv.GetCompression());
	data->data = (char*)dataComp;

	EnterCriticalSection(&sendSect);

	for (auto& i : pcs)
	{
		if (i.IsConnected())
			hnds.push_back(CreateThread(NULL, 0, SendDataEx, construct<SDataEx>(SDataEx(data, i)), CREATE_SUSPENDED, NULL));
	}

	for (HANDLE& h : hnds)
		ResumeThread(h);

	WaitForMultipleObjects(hnds.size(), hnds.data(), true, INFINITE);

	for(HANDLE& h : hnds)
		CloseHandle(h);

	LeaveCriticalSection(&sendSect);


	hnds.clear();
	dealloc(dataComp);
	destruct(data);

	return 0;
}


HANDLE TCPServ::SendClientData(char* data, DWORD nBytes, Socket exAddr, bool single)
{
	return CreateThread(NULL, 0, &SendAllData, (LPVOID)construct<SAData>(SAData(*this, data, nBytes, exAddr, single)), NULL, NULL);
}

HANDLE TCPServ::SendClientData(char* data, DWORD nBytes, std::vector<Socket>& pcs)
{
	return CreateThread(NULL, 0, &SendAllDataEx, (LPVOID)construct<SADataEx>(SADataEx(*this, data, nBytes, pcs)), NULL, NULL);
}


TCPServ::TCPServ(USHORT maxCon, sfunc func, void* obj, int compression, customFunc conFunc, customFunc disFunc)
	:
	host(INVALID_SOCKET),
	openCon(NULL),
	function(func),
	obj(obj),
	compression(compression),
	conFunc(conFunc),
	disFunc(disFunc),
	maxCon(maxCon)
{}

TCPServ::TCPServ(TCPServ&& serv)
	:
	host(serv.host),
	openCon(serv.openCon),
	clients(serv.clients),
	recvIndex(serv.recvIndex),
	function(serv.function),
	obj(serv.obj),
	compression(serv.compression),
	conFunc(serv.conFunc),
	disFunc(serv.disFunc),
	maxCon(serv.maxCon)
{
	ZeroMemory(&serv, sizeof(TCPServ));
}

TCPServ::~TCPServ()
{
	Shutdown();
}

void TCPServ::AllowConnections(const TCHAR* port)
{
	InitializeCriticalSection(&clientSect);
	InitializeCriticalSection(&sendSect);

	host.Bind(port);

	openCon = CreateThread( NULL, 0, WaitForConnections, this, NULL, NULL );
}

void TCPServ::AddClient(Socket pc)
{
	EnterCriticalSection(&clientSect);

	clients.push_back(ClientData(pc, INVALID_HANDLE_VALUE, function));
	Data* data = construct<Data>(Data(*this, clients.size() - 1));
	recvIndex.push_back(&data->pos);
	HANDLE hnd = CreateThread(NULL, 0, RecvData, data, NULL, NULL);
	clients.back().recvThread = hnd;

	RunConFunc(clients.back());

	LeaveCriticalSection(&clientSect);
}

void TCPServ::RemoveClient(USHORT& pos)
{
	EnterCriticalSection(&clientSect);

	const std::tstring user = clients[pos].user;

	RunDisFunc(clients[pos]);

	clients[pos].pc.Disconnect();

	recvIndex[pos] = recvIndex.back();
	*recvIndex[pos] = pos;
	recvIndex.pop_back();

	clients[pos] = clients.back();
	clients.pop_back();

	if (!user.empty())//if user wasnt declined authentication
	{
		UINT nBy;
		const TCHAR* d = _T("has disconnected!");
		TCHAR* msg = FormatMsg(TYPE_CHANGE, MSG_DISCONNECT, (BYTE*)d, _tcslen(d) * sizeof(TCHAR), (std::tstring)user, nBy);
		HANDLE hnd = SendClientData((char*)msg, nBy, Socket(), false);
		WaitAndCloseHandle(hnd);
		dealloc(msg);
	}

	LeaveCriticalSection(&clientSect);
}

void TCPServ::Shutdown()
{
	DeleteCriticalSection(&clientSect);
	DeleteCriticalSection(&sendSect);

	for(USHORT i = 0, size = clients.size(); i < size; i++)
	{
		TerminateThread(clients[i].recvThread, 0);
		CloseHandle(clients[i].recvThread);
		clients[i].pc.Disconnect();
	}

	clients.clear();
	recvIndex.clear();

	host.Disconnect();

	if (openCon)
	{
		TerminateThread(openCon, 0);
		CloseHandle(openCon);
		openCon = NULL;
	}
}

void TCPServ::WaitAndCloseHandle(HANDLE& hnd)
{
	WaitForSingleObject(hnd, INFINITE);
	CloseHandle(hnd);
	hnd = INVALID_HANDLE_VALUE;
}

void TCPServ::Ping(Socket& client)
{
	char msg[] = { TYPE_PING, MSG_PING };
	HANDLE hnd = SendClientData(msg, MSG_OFFSET, client, true);
	WaitAndCloseHandle(hnd);
}

Socket& TCPServ::GetHost()
{
	return host;
}

bool TCPServ::MaxClients() const
{
	return clients.size() == maxCon;
}

std::vector<TCPServ::ClientData>& TCPServ::GetClients()
{
	return clients;
}

void TCPServ::SetFunction(USHORT index, sfunc function)
{
	clients[index].func = function;
}

void TCPServ::RunConFunc(ClientData& client)
{
	conFunc(client);
}

void TCPServ::RunDisFunc(ClientData& client)
{
	disFunc(client);
}

void* TCPServ::GetObj() const
{
	return obj;
}

int TCPServ::GetCompression() const
{
	return compression;
}

std::vector<USHORT*>& TCPServ::GetRecvIndices()
{
	return recvIndex;
}
