#pragma once
#include "Timer.h"
#include "ColorDef.h"

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

class Whiteboard
{
public:
	Whiteboard();
	Whiteboard(HWND WinHandle, USHORT Width, USHORT Height, USHORT FPS, UINT palIndex = 0);
	Whiteboard(Whiteboard&& wb);
	~Whiteboard();

	void Frame(RECT &rect, BYTE *pixelData);
private:
	void InitPalette();
	void InitD3D();
	void BeginFrame();
	void ClearTempSurface();
	void Render(RECT &rect, BYTE *pixelData);
	void ComposeImage(USHORT Width, USHORT Height, BYTE *pixelData);
	void EndFrame();

	D3DCOLOR* GetPalette(BYTE& count);

	HWND hWnd;

	USHORT width, height, pitch;
	float interval;
	Timer timer;
	D3DCOLOR bgColor;
	BYTE *pixels;

	IDirect3D9 *pDirect3D;
	IDirect3DDevice9 *pDevice;
	IDirect3DSurface9 *pBackBuffer;
	D3DCOLOR *tempSurface;
	D3DLOCKED_RECT lockRect;
	D3DCOLOR palette[32];
};

