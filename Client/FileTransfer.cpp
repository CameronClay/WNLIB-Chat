#include "FileTransfer.h"
#include <assert.h>
#include "HeapAlloc.h"
#include "Messages.h"
#include "MsgStream.h"

FileTransfer::FileTransfer(TCPClient& client, HWND wnd, AlertFunc finished, AlertFunc canceled, std::vector<FileMisc::FileData>&& list)
	:
	client(client),
	wnd(wnd),
	list(std::move(list)),
	it(),
	size(0.0),
	progress(0.0),
	dialog(),
	running(false),
	canceled(false),
	username(),
	finishedFunc(finished),
	canceledFunc(canceled)
{}

FileTransfer::FileTransfer(TCPClient& client, HWND wnd, AlertFunc finished, AlertFunc canceled)
	:
	client(client),
	wnd(wnd),
	list(),
	it(),
	size(0.0),
	progress(0.0),
	dialog(),
	running(false),
	canceled(false),
	username(),
	finishedFunc(finished),
	canceledFunc(canceled)
{}

FileTransfer::FileTransfer(FileTransfer&& ft)
	:
	client(ft.client),
	wnd(ft.wnd),
	list(ft.list),
	it(ft.it),
	size(0.0),
	progress(ft.progress),
	dialog(ft.dialog),
	running(ft.running),
	canceled(ft.canceled),
	username(ft.username),
	finishedFunc(ft.finishedFunc),
	canceledFunc(ft.canceledFunc)
{
	ZeroMemory(&ft, sizeof(FileTransfer));
}

FileTransfer::~FileTransfer()
{

}

void FileTransfer::SetList(std::vector<FileMisc::FileData>&& list)
{
	this->list = std::move(list);
}

void FileTransfer::SetSize(double size)
{
	this->size = size;
}

double FileTransfer::GetSize() const
{
	return size;
}

ProgDlg& FileTransfer::GetDialog()
{
	return dialog;
}

HWND FileTransfer::GetWnd() const
{
	return wnd;
}

std::vector<FileMisc::FileData>&  FileTransfer::GetList()
{
	return list;
}

std::vector<FileMisc::FileData>::iterator& FileTransfer::GetIterator()
{
	return it;
}

void FileTransfer::RunFinished()
{
	finishedFunc(username);
}

void FileTransfer::RunCanceled()
{
	canceledFunc(username);
}

void FileTransfer::Stop()
{
	canceled = true;
	running = false;
	size = 0.0;
	progress = 0.0;
	list.clear();
	dialog.Stop();
}

std::tstring& FileTransfer::GetUser()
{
	return username;
}

bool FileTransfer::Running() const
{
	return running;
}





FileSend::FileSend(TCPClient& client, HWND wnd, AlertFunc finished, AlertFunc canceled, std::vector<FileMisc::FileData>&& list, DWORD nBytesPerLoop)
	:
	FileTransfer(client, wnd, finished, canceled, std::move(list)),
	nBytesPerLoop(nBytesPerLoop),
	thread(NULL),
	fullFilepathSrc()
{}

FileSend::FileSend(TCPClient& client, HWND wnd, AlertFunc finished, AlertFunc canceled, DWORD nBytesPerLoop)
	:
	FileTransfer(client, wnd, finished, canceled),
	nBytesPerLoop(nBytesPerLoop),
	thread(NULL),
	fullFilepathSrc()
{}


FileSend::FileSend(FileSend&& ft)
	:
	FileTransfer(std::move(ft)),
	nBytesPerLoop(ft.nBytesPerLoop),
	thread(ft.thread),
	fullFilepathSrc(ft.fullFilepathSrc)
{
	ZeroMemory(&ft, sizeof(FileSend));
}

FileSend::~FileSend()
{
	StopSend();
}

void FileSend::SetFullPathSrc(std::tstring& fullFilepathSrc)
{
	this->fullFilepathSrc = fullFilepathSrc;
}

void FileSend::RequestTransfer()
{
	const UINT nameLen = username.size() + 1;
	const DWORD nameSize = (nameLen * sizeof(TCHAR));
	const DWORD nBytes = sizeof(UINT) + nameSize + sizeof(double);
	MsgStreamWriter streamWriter(TYPE_REQUEST, MSG_REQUEST_TRANSFER, nBytes);

	for(auto& i : list)
		size += i.size;

	size /= (1024 * 1024);

	streamWriter.Write(nameLen);
	streamWriter.Write(username.c_str(), nameSize);
	streamWriter.Write(size);

	HANDLE hnd = client.SendServData(streamWriter, streamWriter.GetSize());
	TCPClient::WaitAndCloseHandle(hnd);

	running = true;
}

void FileSend::SendFileNameList()
{
	canceled = false;
	running = true;

	DWORD nChars = 0;
	for(auto& i : list)
		nChars += i.fileName.size() + 1;

	const UINT userLen = username.size() + 1;
	const DWORD nBytes = ((nChars + userLen) * sizeof(TCHAR)) + ((sizeof(SYSTEMTIME) + sizeof(DWORD64) + sizeof(UINT)) * list.size()) + sizeof(UINT);
	MsgStreamWriter streamWriter(TYPE_FILE, MSG_FILE_LIST, nBytes);
	streamWriter.Write((UINT)userLen);
	streamWriter.Write(username.c_str(), userLen * sizeof(TCHAR));

	for(auto it = list.begin(), end = list.end(); it != end; it++)
	{
		streamWriter.Write(it->size);
		streamWriter.Write(it->dateModified);
		const UINT fileLen = it->fileName.size() + 1;
		streamWriter.Write(fileLen);
		streamWriter.Write(it->fileName.c_str(), fileLen);
	}

	HANDLE hnd = client.SendServData(streamWriter, streamWriter.GetSize());
	TCPClient::WaitAndCloseHandle(hnd);
}

void FileSend::SendCurrentFile()
{
	const bool exists = FileMisc::Exists((fullFilepathSrc + it->fileName).c_str());
	assert(exists);

	if(!exists)
		return;

	File file((fullFilepathSrc + it->fileName).c_str(), FILE_GENERIC_READ);
	const UINT userLen = username.size() + 1;
	const DWORD extraBytesData = sizeof(UINT) + (userLen * sizeof(TCHAR)) + MSG_OFFSET;

	dialog.SetLine1(it->fileName.c_str());

	uqp<char> msgPtr = uqp<char>(alloc<char>(nBytesPerLoop + extraBytesData));
	char* msg = msgPtr.get();

	while(it->size != 0)
	{
		if(dialog.Canceled())
		{
			file.Close();
			client.SendMsg(username, TYPE_FILE, MSG_FILE_SEND_CANCELED);

			RunCanceled();

			StopSend();
		}
		UINT pos = MSG_OFFSET;
		msg[0] = TYPE_FILE;
		msg[1] = MSG_FILE_DATA;
		*(UINT*)&(msg[pos]) = userLen;
		pos += sizeof(UINT);
		memcpy(&msg[pos], username.c_str(), userLen * sizeof(TCHAR));
		pos += userLen * sizeof(TCHAR);

		DWORD bytesRead = file.Read(&msg[pos], nBytesPerLoop);
		progress += ((double)bytesRead) / (double)(1024 * 1024);
		dialog.SetProgress((DWORD)((progress / size) * 100));
		HANDLE hnd = client.SendServData(msg, bytesRead + extraBytesData);
		TCPClient::WaitAndCloseHandle(hnd);
		it->size -= bytesRead;
	}

	file.Close();
}

std::tstring& FileSend::GetFilePathSrc()
{
	return fullFilepathSrc;
}

HANDLE& FileSend::GetThread()
{
	return thread;
}

DWORD CALLBACK SendAllFiles(LPVOID data)
{
	HRESULT res = CoInitialize(NULL);
	assert(SUCCEEDED(res));

	FileSend& send = *(FileSend*)data;
	std::vector<FileMisc::FileData>::iterator& it = send.GetIterator();
	std::vector<FileMisc::FileData>& list = send.GetList();

	send.SendFileNameList();

	it = list.begin();

	const bool result = send.GetDialog().Start(send.GetWnd(), 100, _T("File transfer"), _T("Sending..."), _T("Canceling"));
	assert(result);

	while(it != list.end())
	{
		send.SendCurrentFile();
		++it;
	}

	send.RunFinished();

	send.StopSend();

	CoUninitialize();
	return 0;
}

void FileSend::StartSend()
{
	thread = CreateThread(NULL, 0, SendAllFiles, this, NULL, NULL);
}

void FileSend::StopSend()
{
	CoUninitialize();

	Stop();
	if(thread)
	{
		TerminateThread(thread, 0);
		CloseHandle(thread);
		thread = NULL;
	}	
}

void FileSend::WaitForThread()
{
	WaitForSingleObject(thread, INFINITE);
}





FileReceive::FileReceive(TCPClient& client, HWND wnd, AlertFunc finished, AlertFunc canceled)
	:
	FileTransfer(client, wnd, finished, canceled),
	file(),
	bytesWritten(0)
{
	HRESULT res = CoInitialize(NULL);
	assert(SUCCEEDED(res));
}

FileReceive::FileReceive(FileReceive&& ft)
	:
	FileTransfer(std::move(ft)),
	file(ft.file),
	bytesWritten(ft.bytesWritten)
{
	ZeroMemory(&ft, sizeof(FileReceive));
}

FileReceive::~FileReceive()
{
	CoUninitialize();
	StopReceive();
}

void FileReceive::RecvFileNameList(MsgStreamReader& streamReader, std::tstring& downloadPath)
{
	canceled = false;
	running = true;

	//PROBLEM HERE
	while(!streamReader.End())
	{
		const DWORD64 size = streamReader.Read<DWORD64>();
		const SYSTEMTIME time = streamReader.Read<SYSTEMTIME>();
		const UINT nameLen = streamReader.Read<UINT>();
		std::tstring temp(streamReader.Read<TCHAR>((nameLen - 1) * sizeof(TCHAR)));
		temp.insert(0, downloadPath + _T("\\"));
		list.push_back(FileMisc::FileData(temp, time, size));

	}
	it = list.begin();

	dialog.Start(wnd, 100, _T("File transfer"), _T("Receiving..."), _T("Canceling"));
}

void FileReceive::RecvFile(BYTE* data, DWORD nBytes)
{
	if(canceled)
	{
		file.Close();
		return;
	}

	if(dialog.Canceled())
	{
		file.Close();
		client.SendMsg(username, TYPE_FILE, MSG_FILE_RECEIVE_CANCELED);
		StopReceive();
		RunCanceled();

		return;
	}

	if(!file.IsOpen())//file is closed
	{
		TCHAR *path = alloc<TCHAR>(it->fileName.size() + 1);
		_tcscpy(path, it->fileName.c_str());
		PathRemoveFileSpec(path);
		if(_tcslen(path) && !FileMisc::Exists(path))
			FileMisc::CreateFolder(path);
		dealloc(path);

		GetTempPath(ARRAYSIZE(tempFilename), tempFilename);
		GetTempFileName(tempFilename, _T("cft"), 0, tempFilename);
		file.Open(tempFilename, FILE_GENERIC_WRITE, FILE_ATTRIBUTE_NORMAL, CREATE_ALWAYS);
		file.ChangeDate(it->dateModified);
		dialog.SetLine1(it->fileName.c_str());
	}

	if(bytesWritten == it->size)
	{
		CloseAndReplaceFile();

		++it;
		bytesWritten = 0;
		RecvFile(data, nBytes);

		return;
	}

	const DWORD written = file.Write(data, nBytes);
	bytesWritten += written;
	assert(bytesWritten <= it->size);

	progress += ((double)written) / (double)(1024 * 1024);
	dialog.SetProgress((DWORD)((progress / size) * 100.0));

	if(bytesWritten == it->size && it == --list.end())
	{
		CloseAndReplaceFile();

		StopReceive();

		RunFinished();
	}
}

void FileReceive::CloseAndReplaceFile()
{
	file.Close();

	if(FileMisc::Exists(it->fileName.c_str()))
		ReplaceFile(it->fileName.c_str(), tempFilename, NULL, 0, NULL, NULL);
	else
		FileMisc::MoveOrRename(tempFilename, it->fileName.c_str());
}

void FileReceive::StopReceive()
{
	Stop();
	bytesWritten = 0;
	ZeroMemory(tempFilename, sizeof(TCHAR) * ARRAYSIZE(tempFilename));
}



