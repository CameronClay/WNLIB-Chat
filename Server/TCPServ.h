#pragma once
#include <stdlib.h>
#include <vector>
#include <forward_list>
#include "Socket.h"
#include "Ping.h"

#define INACTIVETIME 30.0f
#define PINGTIME 2.0f

class TCPServ
{
public:
	struct ClientData
	{
		ClientData(Socket pc, sfunc func, USHORT recvIndex)
			:
			pc(pc),
			func(func),
			pingHandler(),
			recvIndex(recvIndex),
			recvThread(INVALID_HANDLE_VALUE)
		{}

		ClientData(ClientData&& clint)
			:
			pc(std::move(clint.pc)),
			func(clint.func),
			pingHandler(std::move(clint.pingHandler)),
			user(std::move(clint.user)),
			recvIndex(clint.recvIndex),
			recvThread(clint.recvThread)
		{
			ZeroMemory(&clint, sizeof(ClientData));
		}

		ClientData& operator=(ClientData&& data)
		{
			if( this != &data )
			{
				pc = std::move( data.pc );
				func = data.func;
				pingHandler = std::move(data.pingHandler);
				user = std::move( data.user );
				recvIndex = data.recvIndex;
				recvThread = data.recvThread;

				ZeroMemory( &data, sizeof( ClientData ) );
			}
			return *this;
		}

		Socket pc;
		sfunc func;
		PingHandler pingHandler;
		std::tstring user;
		USHORT recvIndex;
		HANDLE recvThread;
	};

	typedef void(*const customFunc)(ClientData& data);

	//sfunc is a message handler, compression is 1-9
	TCPServ(USHORT maxCon, sfunc func, void* obj, int compression, customFunc conFunc, customFunc disFunc);
	TCPServ(TCPServ&& serv);
	~TCPServ();

	TCPServ& operator=(TCPServ&& serv);

	void AllowConnections(const TCHAR* port);//Starts receiving as well

	// addr parameter functions as both the excluded address, and as a single address, 
	// depending on the value of single
	HANDLE SendClientData(char* data, DWORD nBytes, Socket addr, bool single);
	HANDLE SendClientData(char* data, DWORD nBytes, Socket* pcs, USHORT nPcs);
	HANDLE SendClientData(char* data, DWORD nBytes, std::vector<Socket>& pcs);

	// send msg funtions used for requests, replies ect. they do not send data
	void SendMsg(Socket pc, bool single, char type, char message);
	void SendMsg(Socket* pcs, USHORT nPcs, char type, char message);
	void SendMsg(std::vector<Socket>& pcs, char type, char message);
	void SendMsg(std::tstring& user, char type, char message);

	void AddClient(Socket pc);
	void RemoveClient(USHORT& pos);
	ClientData* FindClient(std::tstring &user);
	void Shutdown();

	static void WaitAndCloseHandle(HANDLE& hnd);

	void Ping(Socket& client);

	void RunConFunc(ClientData& client);
	void RunDisFunc(ClientData& client);

	ClientData**& GetClients();
	USHORT ClientCount() const;
	void SetFunction(USHORT index, sfunc function);

	CRITICAL_SECTION* GetSendSect();

	Socket& GetHost();
	bool MaxClients() const;
	void* GetObj() const;
	void WaitForRecvThread();
	int GetCompression() const;
private:
	Socket host;
	ClientData** clients;
	USHORT nClients;
	sfunc function;
	void* obj;
	customFunc conFunc, disFunc;
	CRITICAL_SECTION clientSect, sendSect;
	HANDLE openCon;
	const int compression;
	const USHORT maxCon;
};
