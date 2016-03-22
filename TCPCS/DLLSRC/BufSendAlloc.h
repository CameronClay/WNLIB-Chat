#pragma once
#include "CNLIB/BufferOptions.h"
#include "CNLIB/StreamAllocInterface.h"
#include "WSABufExt.h"
#include "CNLIB/CompressionTypes.h"
#include "CNLIB/MemPool.h"
#include "CNLIB/HeapAlloc.h"
#include "CNLIB/BuffAllocator.h"
#include "CNLIB/BuffSendInfo.h"

class DataPoolAllocator : public BuffAllocator
{
public:
	DataPoolAllocator(const BufferOptions& bufferOptions, UINT maxDataSize = 4096, UINT sendBuffCount = 35);
	DataPoolAllocator(DataPoolAllocator&& dataPoolObs);
	DataPoolAllocator& operator=(DataPoolAllocator&& dataPoolObs);

	void dealloc(char* data, DWORD nBytes = 0) override;
	char* alloc(DWORD = NULL) override;
private:
	MemPoolSync<PageAllignAllocator> sendDataPool; //Used to help speed up allocation of send buffers
};

class DataCompPoolAllocator : public BuffAllocator
{
public:
	DataCompPoolAllocator(const BufferOptions& bufferOptions, UINT maxDataSize = 4096, UINT sendCompBuffCount = 15);
	DataCompPoolAllocator(DataCompPoolAllocator&& dataPoolObs);
	DataCompPoolAllocator& operator=(DataCompPoolAllocator&& dataPoolObs);

	void dealloc(char* data, DWORD nBytes = 0) override;
	char* alloc(DWORD = NULL) override;
private:
	MemPoolSync<PageAllignAllocator> sendDataCompPool; //Used to help speed up allocation of send buffers
};

class MsgPoolAllocator : public BuffAllocator
{
public:
	MsgPoolAllocator(UINT sendMsgBuffCount = 10);
	MsgPoolAllocator(MsgPoolAllocator&& msgPoolObs);
	MsgPoolAllocator& operator=(MsgPoolAllocator&& msgPoolObs);

	void dealloc(char* data, DWORD nBytes = 0) override;
	char* alloc(DWORD = NULL) override;
private:
	MemPoolSync<HeapAllocator> sendMsgPool; //Used to help speed up allocation of send buffers
};

class BufSendAlloc : public StreamAllocInterface
{
public:
	BufSendAlloc(UINT maxDataSize = 4096, UINT sendBuffCount = 35, UINT sendCompBuffCount = 15, UINT sendMsgBuffCount = 10, int compression = 9, int compressionCO = 512);
	BufSendAlloc(const BufSendAlloc&) = delete;
	BufSendAlloc(BufSendAlloc&& bufSendAlloc);
	BufSendAlloc& operator=(BufSendAlloc&& bufSendAlloc);

	BuffSendInfo GetSendBuffer(DWORD hiByteEstimate, CompressionType compType = BESTFIT) override;
	BuffSendInfo GetSendBuffer(BuffAllocator* alloc, DWORD nBytes, CompressionType compType = BESTFIT) override;

	MsgStreamWriter CreateOutStream(DWORD hiByteEstimate, short type, short msg, CompressionType compType = BESTFIT) override;
	MsgStreamWriter CreateOutStream(BuffAllocator* alloc, DWORD nBytes, short type, short msg, CompressionType compType = BESTFIT) override;

	const BufferOptions GetBufferOptions() const override;

	WSABufSend CreateBuff(const BuffSendInfo& buffSendInfo, DWORD nBytesDecomp, bool msg, USHORT index = -1, BuffAllocator* alloc = nullptr);
	void FreeBuff(WSABufSend& buff);
private:
	const BufferOptions bufferOptions;
	std::atomic<USHORT> bufIndex;
	DataPoolAllocator dataPool;
	DataCompPoolAllocator dataCompPool;
	MsgPoolAllocator msgPool;
};