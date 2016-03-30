#include "Stdafx.h"
#include "RecvHandler.h"
#include "CNLIB/MsgHeader.h"
#include "CNLIB/MsgStream.h"
#include "CNLIB/File.h"


//To do: have only one compressed buffer
RecvHandler::RecvHandler(const BufferOptions& buffOpts, UINT initialCap, RecvObserverI* observer)
	:
	recvBuffPool(buffOpts.GetMaxDatBuffSize(), initialCap + 1, buffOpts.GetPageSize()),  //only need maxDataSize, because compData buffer takes care of decompression
	decompData(recvBuffPool.alloc<char>()),
	ol(OpType::recv),
	primaryBuff(CreateBuffer(buffOpts)),
	secondaryBuff(CreateBuffer(buffOpts)),
	curBuff(&primaryBuff),
	nextBuff(&secondaryBuff),
	savedBuff(),
	observer(observer)
{}

RecvHandler::RecvHandler(RecvHandler&& recvHandler)
	:
	recvBuffPool(std::move(recvHandler.recvBuffPool)),
	decompData(recvHandler.decompData),
	ol(recvHandler.ol),
	primaryBuff(recvHandler.primaryBuff),
	secondaryBuff(recvHandler.secondaryBuff),
	curBuff(recvHandler.curBuff),
	nextBuff(recvHandler.nextBuff),
	savedBuff(recvHandler.savedBuff),
	observer(recvHandler.observer)
{
	memset(&recvHandler, 0, sizeof(RecvHandler));
}

RecvHandler::~RecvHandler()
{
	//no need to free objects from pool because they will die when pool dies
}

RecvHandler& RecvHandler::operator=(RecvHandler&& recvHandler)
{
	if (this != &recvHandler)
	{
		this->~RecvHandler();

		recvBuffPool = std::move(recvHandler.recvBuffPool);
		decompData = recvHandler.decompData;
		ol = recvHandler.ol;
		primaryBuff = recvHandler.primaryBuff;
		secondaryBuff = recvHandler.secondaryBuff;
		curBuff = recvHandler.curBuff;
		nextBuff = recvHandler.nextBuff;
		observer = recvHandler.observer;

		memset(&recvHandler, 0, sizeof(RecvHandler));
	}

	return *this;
}

void RecvHandler::StartRead(Socket& pc)
{
	pc.ReadDataOl(curBuff, &ol);
}

bool RecvHandler::RecvDataCR(Socket& pc, DWORD bytesTrans, const BufferOptions& buffOpts, void* obj)
{
	//spin for a bit if buffer is still being used
	//or I guess could wrap entire thing in crit sect
	//guess a sleep would be acceptable because it would wake another iocp thread
	while (curBuff->used)
		Sleep(1);

	curBuff->used = true;

	pc.ReadDataOl(nextBuff, &ol);

	char* ptr = curBuff->head;

	do
	{
		if (!(ptr = RecvData(bytesTrans, ptr, buffOpts, obj)))
		{
			curBuff->used = false;
			return false;
		}
	} while (bytesTrans);

	std::swap(curBuff, nextBuff);

	curBuff->used = false;
	
	return true;
}

char* RecvHandler::RecvData(DWORD& bytesTrans, char* ptr, const BufferOptions& buffOpts, void* obj)
{
	WSABufRecv& srcBuff = *curBuff;

	if (savedBuff.curBytes)
	{
		DataHeader& header = *(DataHeader*)savedBuff.head;
		const DWORD bytesToRecv = ((header.size.up.nBytesComp) ? header.size.up.nBytesComp : header.size.up.nBytesDecomp);
		bytesTrans = AppendBuffer(ptr, srcBuff, savedBuff, bytesToRecv, bytesTrans);

		if (savedBuff.curBytes >= bytesToRecv)
		{
			ptr += sizeof(DataHeader);
			Process(savedBuff.head + sizeof(DataHeader), savedBuff, bytesToRecv, header, buffOpts, obj);

			FreeBuffer(savedBuff);
			srcBuff.curBytes = 0;
			bytesTrans = 0;

			return ptr + bytesToRecv;
		}

		return ptr;
	}
	else
	{
		if (bytesTrans < sizeof(DataHeader))
		{
			if (srcBuff.head != ptr)
				memcpy(srcBuff.head, ptr, bytesTrans);

			srcBuff.curBytes = bytesTrans;
			SaveBuff(srcBuff, true, buffOpts);

			bytesTrans = 0;
			return ptr;		
		}

		DataHeader& header = *(DataHeader*)ptr;
		const DWORD bytesToRecv = ((header.size.up.nBytesComp) ? header.size.up.nBytesComp : header.size.up.nBytesDecomp);

		//If there is a full data block ready for processing
		if (bytesTrans - sizeof(DataHeader) >= bytesToRecv)
		{
			bytesTrans -= bytesToRecv + sizeof(DataHeader);
			ptr += sizeof(DataHeader);

			srcBuff.curBytes = 0;
	
			return Process(ptr, srcBuff, bytesToRecv, header, buffOpts, obj);
		}

		if (bytesToRecv <= buffOpts.GetMaxDatBuffSize())
		{
			//Concatenate remaining data to front of buffer
			if (srcBuff.head != ptr)
				memcpy(srcBuff.head, ptr, bytesTrans);

			srcBuff.curBytes = bytesTrans;
			SaveBuff(srcBuff, true, buffOpts);
		}
		else
		{
			char* buffer = alloc<char>(bytesToRecv + sizeof(DataHeader));
			memcpy(buffer, ptr, bytesTrans);

			WSABufRecv buff;
			buff.Initialize(bytesToRecv, buffer, buffer, bytesTrans - sizeof(DataHeader));

			SaveBuff(buff, false, buffOpts);
		}

		bytesTrans = 0;
		return ptr;
	}
}

char* RecvHandler::Process(char* ptr, WSABufRecv& buff, DWORD bytesToRecv, const DataHeader& header, const BufferOptions& buffOpts, void* obj)
{
	//If data was compressed
	if (header.size.up.nBytesComp)
	{
		char* dest = (header.size.up.nBytesDecomp > buffOpts.GetMaxDataSize()) ? alloc<char>(header.size.up.nBytesDecomp) : decompData;

		//Max Data size because it sends decomp if > than maxDataSize
		if (FileMisc::Decompress((BYTE*)dest, header.size.up.nBytesDecomp, (const BYTE*)ptr, bytesToRecv) != UINT_MAX)	// Decompress data
		{
			observer->OnNotify(dest, header.size.up.nBytesDecomp, obj);
			if (dest != decompData)
				dealloc(dest);

			return ptr + bytesToRecv;
		}

		return nullptr;  //return nullptr if compression failed
	}

	//If data was not compressed
	observer->OnNotify(ptr, header.size.up.nBytesDecomp, obj);
	return ptr + bytesToRecv;
}

DWORD RecvHandler::AppendBuffer(char* ptr, WSABufRecv& srcBuff, WSABufRecv& destBuff, DWORD bytesToRecv, DWORD bytesTrans)
{
	const DWORD temp = min(bytesToRecv - destBuff.curBytes, bytesTrans);
	memcpy(destBuff.head + destBuff.curBytes + sizeof(DataHeader), ptr, temp);
	destBuff.curBytes += temp;

	return bytesTrans - temp;
}

void RecvHandler::SaveBuff(const WSABufRecv& buff, bool newBuff, const BufferOptions& buffOpts)
{
	if (newBuff)
		*curBuff = CreateBuffer(buffOpts);

	savedBuff = buff;
}


WSABufRecv RecvHandler::CreateBuffer(const BufferOptions& buffOpts)
{
	char* temp = recvBuffPool.alloc<char>();
	WSABufRecv buff;
	buff.Initialize(buffOpts.GetMaxDatBuffSize(), temp, temp);
	return buff;
}

void RecvHandler::FreeBuffer(WSABufRecv& buff)
{
	if (recvBuffPool.InPool(buff.head))
		recvBuffPool.dealloc(buff.head);
	else
		dealloc(buff.head);

	savedBuff.curBytes = 0;
}