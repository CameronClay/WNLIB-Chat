#pragma once
#include "CNLIB/MemPool.h"
#include "CNLIB/HeapAlloc.h"
#include "WSABufExt.h"
#include "CNLIB/BufferOptions.h"
#include "CNLIB/Socket.h"
#include "CNLIB/OverlappedExt.h"
#include "CNLIB/MsgHeader.h"
#include "RecvObserverI.h"

class RecvHandler
{
public:
	RecvHandler(const BufferOptions& buffOpts, MemPool<HeapAllocator>& recvPoolPool, UINT initialCap, RecvObserverI* observer);
	RecvHandler(RecvHandler&& recvHandler);
	~RecvHandler();

	RecvHandler& operator=(RecvHandler&& recvHandler);

	void StartRead(Socket& pc);

	bool RecvDataCR(Socket& pc, DWORD bytesTrans, const BufferOptions& buffOpts, void* obj = nullptr);
private:
	char* RecvData(DWORD& bytesTrans, char* ptr, std::unordered_map<UINT, WSABufRecv>::iterator it, const DataHeader& header, const BufferOptions& buffOpts, void* obj);
	bool Process(char* ptr, WSABufRecv& buff, DWORD bytesToRecv, const DataHeader& header, const BufferOptions& buffOpts, void* obj);

	DWORD AppendBuffer(char* ptr, WSABufRecv& srcBuff, WSABufRecv& destBuff, DWORD bytesToRecv, DWORD bytesTrans);

	WSABufRecv CreateBuffer(const BufferOptions& buffOpts);
	void FreeBuffer(WSABufRecv& buff);

	void EraseFromMap(std::unordered_map<UINT, WSABufRecv>::iterator it);
	void AppendToMap(UINT index, WSABufRecv& buff, const BufferOptions& buffOpts);

	MemPool<HeapAllocator>& recvPoolPool;
	MemPool<PageAllignAllocator>* recvBuffPool;
	char* decompData;
	std::unordered_map<UINT, WSABufRecv> buffMap;
	OverlappedExt ol;
	WSABufRecv primaryBuff, secondaryBuff;
	WSABufRecv *curBuff, *nextBuff;
	RecvObserverI* observer;
};