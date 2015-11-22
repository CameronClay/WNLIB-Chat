#include "Socket.h"

#include <ws2tcpip.h>
#include <Mstcpip.h>
#include <functional>

#include "HeapAlloc.h"
#include "File.h"

int InitializeNetworking()
{
	WSADATA wsad{};
	return WSAStartup(MAKEWORD(2, 2), &wsad);
}

int CleanupNetworking()
{
	return WSACleanup();
}

Socket::Socket(const LIB_TCHAR* port)
{
	Bind(port);
}


Socket::Socket()
	:
	pc(INVALID_SOCKET)
{}

Socket::Socket(SOCKET pc)
	:
	pc(pc)
{}

Socket::Socket(Socket&& sock)
	:
	pc(sock.pc)
{}

Socket::~Socket(){}


Socket& Socket::operator= (const Socket& pc)
{
	this->pc = pc.pc;
	return *this;
}
Socket& Socket::operator= (Socket&& pc)
{
	if(this != &pc)
	{
		this->pc = pc.pc;
		ZeroMemory(&pc, sizeof(Socket));
	}
	return *this;
}

bool Socket::operator== (const Socket pc) const
{
	return pc.pc == this->pc;
}
bool Socket::operator!= (const Socket pc) const
{
	return pc.pc != this->pc;
}
bool Socket::operator== (const SOCKET pc) const
{
	return pc == this->pc;
}
bool Socket::operator!= (const SOCKET pc) const
{
	return pc != this->pc;
}

Socket::operator SOCKET&()
{
	return pc;
}

Socket::operator HANDLE&()
{
	return (HANDLE&)pc;
}

bool Socket::Bind(const LIB_TCHAR* port)
{
	ADDRINFOT info = { AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP };
	ADDRINFOT* addr = 0;
	GetAddrInfo(NULL, port, &info, &addr);

	bool result = true;
	for (ADDRINFOT* tempPtr = addr; tempPtr->ai_next != nullptr; tempPtr = tempPtr->ai_next)
	{
		ADDRINFOT& temp = *tempPtr;
		if ((temp.ai_family == AF_INET) || (temp.ai_family == AF_INET6))
		{
			pc = socket(temp.ai_family, temp.ai_socktype, temp.ai_protocol);
			if (result &= (pc != INVALID_SOCKET))
				if (result &= (bind(pc, temp.ai_addr, temp.ai_addrlen) == 0))
					result &= (listen(pc, SOMAXCONN) == 0);					
		}
	}

	FreeAddrInfo(addr);

	return result;
}

Socket Socket::AcceptConnection()
{
	if(IsConnected())
	{
		Socket temp(accept(pc, NULL, NULL));
		if(temp.IsConnected())
			return temp;
	}
	return Socket();
}


//IP or HostName for dest
bool Socket::Connect(const LIB_TCHAR* dest, const LIB_TCHAR* port, float timeout)
{
	int result = false;
	ADDRINFOT* addr = 0;
	ADDRINFOT info = { 0, AF_INET, SOCK_STREAM, IPPROTO_TCP };
	//info.ai_flags = AI_NUMERICHOST;
	result = GetAddrInfo(dest, port, &info, &addr);
	if(!result)
	{
		pc = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if(pc != INVALID_SOCKET)
		{
			SetNonBlocking();
			result = connect(pc, addr->ai_addr, addr->ai_addrlen);
			if(result == SOCKET_ERROR)
			{
				FD_SET fds;
				FD_ZERO(&fds);
				FD_SET(pc, &fds);
				u_long sec = (long)floor(timeout);
				TIMEVAL tv = { sec, long((timeout - sec) * 100000.f) };
				if(select(0, nullptr, &fds, nullptr, &tv) <= 0)
				{
					closesocket(pc);
					pc = INVALID_SOCKET;
				}
				else
				{
					SetBlocking();
				}
			}
			else
			{
				SetBlocking();
			}
		}
		FreeAddrInfo(addr);
	}

	return IsConnected();
}

void Socket::Disconnect()
{
	if(IsConnected())
	{
		shutdown(pc, SD_BOTH);
		closesocket(pc);
		pc = INVALID_SOCKET;
	}
}


long Socket::ReadData(void* dest, DWORD nBytes)
{
#if NTDDI_VERSION >= NTDDI_VISTA
	return recv(pc, (char*)dest, nBytes, MSG_WAITALL);
#else
	long received = 0, temp = SOCKET_ERROR;
	do
	{
		received += temp = recv(pc, ((char*)dest) + received, nBytes - received, 0);
		if ((temp == 0) || (temp == SOCKET_ERROR))
			return temp;
	} while (received != nBytes);
	return received;
#endif
}

long Socket::SendData(const void* data, DWORD nBytes)
{
	long sent = 0, temp = SOCKET_ERROR;
	do
	{
		sent += temp = send(pc, ((char*)data) + sent, nBytes - sent, 0);
		if (temp == SOCKET_ERROR)
			return temp;
	} while(sent != nBytes);
	return sent;
}


bool Socket::ToIp(LIB_TCHAR* ipaddr) const
{
	sockaddr_in saddr = {};
	int len = sizeof(saddr);
	bool res = (getpeername(pc, (sockaddr*)&saddr, &len) != SOCKET_ERROR);
#if NTDDI_VERSION >= NTDDI_VISTA
	InetNtop(saddr.sin_family, &saddr.sin_addr, ipaddr, INET_ADDRSTRLEN);
#else
	Inet_ntot(saddr.sin_addr, ipaddr);
#endif

	return res;
}

bool Socket::IsConnected() const
{
	return pc != INVALID_SOCKET;
}


bool Socket::GetLocalIP(LIB_TCHAR* dest, DWORD buffSize, bool ipv6)
{
	LIB_TCHAR buffer[128] = {};
	ADDRINFOT info = { 0, ipv6 ? AF_INET6 : AF_INET }, *pa = nullptr;
	GetHostName(buffer, 128);
	bool res = (GetAddrInfo(buffer, NULL, &info, &pa) == 0);

	if (res)
		res &= WSAAddressToString(pa->ai_addr, pa->ai_addrlen, NULL, dest, &buffSize);

	FreeAddrInfo(pa);

	return  res;
}

bool Socket::HostNameToIP(const LIB_TCHAR* host, LIB_TCHAR* dest, DWORD buffSize, bool ipv6)
{
	ADDRINFOT info = { 0, ipv6 ? AF_INET6 : AF_INET }, *pa = nullptr;
	bool res = (GetAddrInfo(host, NULL, &info, &pa) == 0);

	if (res)
		res &= WSAAddressToString(pa->ai_addr, pa->ai_addrlen, NULL, dest, &buffSize);

	FreeAddrInfo(pa);

	return res;
}

bool Socket::SetBlocking()
{
	u_long nbio = 0;
	return (ioctlsocket(pc, FIONBIO, &nbio) != SOCKET_ERROR);
}

bool Socket::SetNonBlocking()
{
	u_long nbio = 1;
	return (ioctlsocket(pc, FIONBIO, &nbio) != SOCKET_ERROR);
}

