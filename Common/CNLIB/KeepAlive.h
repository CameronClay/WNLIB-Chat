//Copyright (c) <2015> <Cameron Clay>

#pragma once
#include "Socket.h"

class TCPServ;

class CAMSNETLIB KeepAliveHI
{
public:
	virtual void KeepAlive() = 0;
	virtual void SetKeepAliveInterval(float interval) = 0;
	virtual float GetKeepAliveInterval() const = 0;
};

class CAMSNETLIB KeepAliveHandler
{
public:
	struct CAMSNETLIB KeepAliveData
	{
		KeepAliveData(KeepAliveHI* keepAliveHI, KeepAliveHandler* keepAliveHandler);
		KeepAliveData(KeepAliveData&& keepAliveData);
		KeepAliveData& operator=(KeepAliveData&& data);

		KeepAliveHI* keepAliveHI;
		KeepAliveHandler* keepAliveHandler;
	};

	KeepAliveHandler& operator=(KeepAliveHandler&& data);
	KeepAliveHandler& operator=(const KeepAliveHandler& data);

	KeepAliveHandler(KeepAliveHI* keepAliveHI);
	KeepAliveHandler(KeepAliveHandler&& keepAlive);
	~KeepAliveHandler();

	void SetKeepAliveTimer(float interval);

	HANDLE GetKeepAliveTimer() const;

private:
	HANDLE keepAliveTimer, keepAliveThread;
	DWORD keepAliveID;
	KeepAliveData* keepAliveData;
};
