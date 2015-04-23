#include "Whiteboard.h"


Whiteboard::Whiteboard(TCPServ &serv, USHORT ScreenWidth, USHORT ScreenHeight, USHORT Fps)
	:
screenWidth(ScreenWidth),
screenHeight(ScreenHeight),
fps(Fps),
serv(serv)/*,
pFactory(nullptr),
pWicFactory(nullptr),
pRenderTarget(nullptr),
pWicBitmap(nullptr)*/
{
	/*HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pWicFactory)
		);

	pWicFactory->CreateBitmap(
		800, 
		600, 
		GUID_WICPixelFormat32bppPBGRA, 
		WICBitmapCacheOnDemand, 
		&pWicBitmap
		);

	D2D1CreateFactory(
		D2D1_FACTORY_TYPE_MULTI_THREADED, 
		&pFactory
		);

	pFactory->CreateWicBitmapRenderTarget(
		pWicBitmap, 
		D2D1::RenderTargetProperties(), 
		&pRenderTarget
		);*/
	InitializeCriticalSection(&bitmapSect);
	InitializeCriticalSection(&mapSect);
}

Whiteboard::Whiteboard(Whiteboard &&wb) :
screenWidth(wb.screenWidth),
screenHeight(wb.screenHeight),
fps(wb.fps),
pixels(pixels),
bitmapSect(wb.bitmapSect),
mapSect(wb.mapSect),
clients(wb.clients),
serv(wb.serv) // Don't think copying refs works
{
	ZeroMemory(&wb, sizeof(Whiteboard));
}

BYTE *Whiteboard::GetBitmap()
{
	EnterCriticalSection(&bitmapSect);
	return nullptr;
	LeaveCriticalSection(&bitmapSect);
}

CRITICAL_SECTION& Whiteboard::GetMapSect()
{
	return mapSect;
}

void Whiteboard::PaintBrush(std::deque<PointU> &pointList, BYTE clr)
{
	for (int i = 0; i < pointList.size(); i++)
	{
		if (pointList.size() >= 2)
		{
			MakeRect(pointList[0], pointList[1]);

			DrawLine(pointList[0], pointList[1], clr);

			pointList.pop_front();
		}
		else if (pointList.size() == 1)
		{
			PointU p0, p1;

			p0.x = pointList[0].x - 1;
			p0.y = pointList[0].y - 1;
			p1.x = pointList[0].x + 1;
			p1.y = pointList[0].y + 1;

			MakeRect(p0, p1);

			DrawLine(p0, p1, clr);

			pointList.pop_front();
		}
	}
}

void Whiteboard::Draw()
{
	std::vector<TCPServ::ClientData> & cList = serv.GetClients();

	for (int i = 0; i < cList.size(); i++)
	{
		Socket &skt = cList[i].pc;
		MouseClient mouse(clients[skt].mServ);
		Tool myTool = clients[skt].tool;
		BYTE color = clients[skt].clrIndex;

		std::deque<PointU> pointList;
		while (mouse.Read().GetType() != MouseEvent::Type::Invalid)
		{
			PointU pt;
			pt.x = mouse.GetX();
			pt.y = mouse.GetY();
			pointList.push_back(pt);
		}

		switch (myTool)
		{
		case Tool::PaintBrush:
			PaintBrush(pointList, color);
			break;
		}

		pointList.clear();
	}
}

void Whiteboard::DrawLine(PointU start, PointU end, BYTE clr)
{
	PointU dist = end - start;
	float len = dist.Length();
	dist = dist.Normalize();	

	for (float i = 0.0f; i < len; i++)
	{
		PointU pos = (dist * i) + start;

		int index = (pos.y * screenWidth) + pos.x;
		pixels[index] = clr;
	}
}

void Whiteboard::MakeRect(PointU &p0, PointU &p1)
{
	RectU rect;

	p0.x < p1.x ?
		rect.left = p0.x, rect.right = p1.x :
		rect.left = p1.x, rect.right = p0.x;

	p0.y < p1.y ?
		rect.top = p0.y, rect.bottom = p1.y :
		rect.top = p1.y, rect.bottom = p0.y;

	rectList.push_back(rect);
}

std::unordered_map<Socket, WBClientData, Socket::Hash>& Whiteboard::GetMap()
{
	return clients;
}

Whiteboard::~Whiteboard()
{
	//need a way to check if has been inited for mctor
	//if()
	{
		DeleteCriticalSection(&bitmapSect);
	}
	//if()
	{
		DeleteCriticalSection(&mapSect);
	}
}
