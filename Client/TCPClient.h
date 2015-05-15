#pragma once
#include "Socket.h"
#include <tchar.h>

class TCPClient
{
public:
	//cfunc is a message handler, compression 1-9
	TCPClient(cfunc func, void(*const disconFunc)(), int compression = 9, void* obj = nullptr);
	TCPClient(TCPClient&& client);
	~TCPClient();

	TCPClient& operator=(TCPClient&& client);


	bool Connect(const TCHAR* dest, const TCHAR* port, float timeOut);
	void Disconnect();

	HANDLE SendServData(const char* data, DWORD nBytes);

	//Should only be called once to intialy create receving thread
	bool RecvServData();

	//send msg funtions used for requests, replies ect. they do not send data
	void SendMsg(char type, char message);
	void SendMsg(std::tstring& user, char type, char message);

	//Used to wait and close the handle to a thread
	static void WaitAndCloseHandle(HANDLE& hnd);

	//Should be called in function/msgHandler when ping msg is received from server
	void Ping();

	void SetFunction(cfunc function);

	void* GetObj() const;
	cfuncP GetFunction();
	Socket& GetHost();
	void WaitForRecvThread() const;
	int GetCompression() const;
	CRITICAL_SECTION* GetSendSect();

	void(*GetDisfunc()) ();
	bool IsConnected() const;

private:
	Socket host; //server/host you are connected to
	cfunc function; //function/msgHandler
	void(*const disconFunc)(); //function called when disconnect occurs
	void* obj; //passed to function/msgHandler for oop programming
	HANDLE recv; //thread to receving packets from server
	CRITICAL_SECTION sendSect; //used for synchonization
	const int compression; //compression client sends packets at
};
