#pragma once
#include "CNLIB\Typedefs.h"
#include <Windows.h>
#include <stdlib.h>
#include <tchar.h>

class TextDisplay
{
public:
	TextDisplay()
		:
		hnd(NULL),
		text()
	{}

	void SetHandle(HWND hnd)
	{
		this->hnd = hnd;
	}
	void WriteLine(LIB_TCHAR* buffer, DWORD length)
	{
		if (!text.empty())
			text.append(_T("\r\n"));

		text.append(buffer, length);

		SendMessage(hnd, WM_SETTEXT, 0, (LPARAM)text.c_str());
		SendMessage(hnd, EM_LINESCROLL, 0, MAXLONG);
	}
	const std::tstring GetText()
	{
		return text;
	}
private:
	HWND hnd;
	std::tstring text;
};