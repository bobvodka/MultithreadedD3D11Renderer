// D3D11MTDXGITest.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <Windowsx.h>
#include <Mmsystem.h>
#include "D3D11MTDXGITest.h"

#include <d3d11.h>
#include <d3dCompiler.h>
#include <d3dx11.h>
#include <d3dx9math.h>
#include <DxErr.h>
#include <D3D11SDKLayers.h>

#include <agents.h>
#include <ppl.h>
#include <concurrent_vector.h>

#include "RenderingAgent.h"

// Headers for visualiser stuffs

#include <xnamath.h>
#include <fmod.hpp>
#include <algorithm>

#ifdef _DEBUG
#pragma comment( lib, "fmodexL_vc.lib" )
#else
#pragma comment( lib, "fmodex_vc.lib")
#endif

#include <functional>
using namespace std::placeholders;

// Particle stuffs
#include "Shard Engine/Emitter.h"
#include "Shard Engine/EmitterHookDetails.h"
#include "IScheduler.h"

// temp log stuff
#include <fstream>

//--------------------------------------------------------------------------------------
// Display error msg box to help debug 
//--------------------------------------------------------------------------------------
HRESULT WINAPI DXUTTrace( const CHAR* strFile, DWORD dwLine, HRESULT hr,
	const WCHAR* strMsg, bool bPopMsgBox )
{
	bPopMsgBox = false;

	return DXTrace( strFile, dwLine, hr, strMsg, bPopMsgBox );
}

#if defined(DEBUG) || defined(_DEBUG)
#ifndef V
#define V(x)           { hr = (x); if( FAILED(hr) ) { DXUTTrace( __FILE__, (DWORD)__LINE__, hr, L#x, true ); } }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = (x); if( FAILED(hr) ) { return DXUTTrace( __FILE__, (DWORD)__LINE__, hr, L#x, true ); } }
#endif
#else
#ifndef V
#define V(x)           { hr = (x); }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = (x); if( FAILED(hr) ) { return hr; } }
#endif
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)       { if (p) { delete (p);     (p)=NULL; } }
#endif
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) { if (p) { delete[] (p);   (p)=NULL; } }
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p)=NULL; } }
#endif


// end

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int,HWND&);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);

// D3D11 related options
HRESULT SetupD3D11(HWND);
void ShutDownD3D();

ID3D11DeviceContext*				g_pImmediateContext = NULL;
ID3D11DeviceContext*				g_pDeferredContext = NULL;
D3D_DRIVER_TYPE                   	g_driverType = D3D_DRIVER_TYPE_NULL;
ID3D11Device*                       g_pd3dDevice = NULL;
IDXGISwapChain*                     g_pSwapChain = NULL;
ID3D11RenderTargetView*             g_pRenderTargetView = NULL;
// End D3D11 stuff

// Bunch of D3D stuff we need for the visualiser testing
HRESULT SetupD3D11TestResources(ID3D11Device* pd3dDevice, const D3D11_TEXTURE2D_DESC* pBackBufferSurfaceDesc, int width, int height);
void SetupDeferredContext(ID3D11DeviceContext* context, int width, int height);

ID3D11InputLayout*          g_pVertexLayout11 = NULL;
ID3D11Buffer*               g_pVertexBuffer = NULL;
ID3D11Buffer*               g_pIndexBuffer = NULL;
ID3D11VertexShader*         g_pVertexShader = NULL;
ID3D11PixelShader*          g_pPixelShader = NULL;
ID3D11ComputeShader*			g_pComputeShader = NULL;
ID3D11SamplerState*         g_pSamLinear = NULL;

struct CB_VS_PER_OBJECT
{
	XMMATRIX m_WorldViewProj;
	//	D3DXMATRIX m_World;
};
UINT                        g_iCBVSPerObjectBind = 0;

// struct CB_PS_PER_OBJECT
// {
// 	D3DXVECTOR4 m_vObjectColor;
// };
// UINT                        g_iCBPSPerObjectBind = 0;
// 
// struct CB_PS_PER_FRAME
// {
// 	D3DXVECTOR4 m_vLightDirAmbient;
// };
// UINT                        g_iCBPSPerFrameBind = 1;

ID3D11Buffer*               g_pcbVSPerObject = NULL;
ID3D11Buffer*               g_pcbPSPerObject = NULL;
ID3D11Buffer*               g_pcbPSPerFrame = NULL;

// compute shader buffers etc
struct TLMDataBuffer
{
	D3DXVECTOR4	m_energyLevel;		// output energy level from this stage going to the four nodes around me
//	D3DXVECTOR4 m_outputColours[4];	// The colours we are sending to each node
};

struct TLMInputData
{
	float	m_driveValue;			// value to insert into the simulation
	int position;				// location on the grid for this data point
//	D3DXVECTOR4	m_colour;			// colour to send with it
};

struct QuadVertex
{
	D3DXVECTOR3 pos;
	D3DXVECTOR2 uv;
};

ID3D11Buffer*				g_pTLMMap[2] = { NULL, NULL };		// source/sink for energy data + colour data
ID3D11Buffer*				g_pTLMDriveDataBuffer = NULL;		// input into TLM simulation
ID3D11Texture2D*			g_pColourOutTexure = NULL;			// colour output buffer
int							dataSource[] = { 0, 1};

ID3D11ShaderResourceView*	g_pTLMInput[] = { NULL, NULL };
ID3D11ShaderResourceView*	g_pTLMDriveData = NULL;
ID3D11UnorderedAccessView*	g_pColourOutput = NULL;
ID3D11UnorderedAccessView*	g_pTLMOutput[] = { NULL, NULL };
ID3D11ShaderResourceView*	g_pQuadColourSource = NULL;

enum SendMode
{
	SendMode_DirectSend,
	SendMode_MixedSend,
	SendMode_FullDeferred

};

bool g_singleThread = false;
SendMode g_SendMode = SendMode_FullDeferred; // SendMode_DirectSend;
struct TLMDispatch
{
	int x, y;
} tlmDispatch;

// FMOD stuff
FMOD::System * fmodSystem;
#define ERRCHECK(res) if(res != FMOD_OK) { 	char error[256]; sprintf_s(error,256,"FMOD Error: %d\n", res); OutputDebugStringA(error); return -1; } 
#define DebugOutputInt(val) {char error[256]; sprintf_s(error,256, "Rendering commands processed: %d\n", val); OutputDebugStringA(error); }
#define DebugOutput(val) { char error[256]; sprintf_s(error, 256,val); OutputDebugStringA(error); }

// Misc Rendering stuff
XMMATRIX projectMatrix;
// end of stuff

float leftSpectrum[2048];
float rightSpectrum[2048];

struct SizeToMod
{
	int size;
	int mod;
};
//                           0			1			2			3			4
SizeToMod modValue[] = { {2048, 75} , {1024, 150}, {512, 300}, {256, 600}, {128, 1200} };

const int modSelection = 0;

float bands[2][10] = 
{
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}
};

const float bandlimits[10] = { 32.3f, 64.6f, 129.2f, 258.4f, 516.8f, 1033.6f, 2067.2f, 4134.4f, 8268.8f, /*16537.5f*/ 999999.0f  };

struct Point
{
	Point(int x, int y) : x(x), y(y)
	{

	}
	Point(const Point &rhs) : x(rhs.x), y(rhs.y)
	{

	}
	Point(Point &&rhs): x(rhs.x), y(rhs.y)
	{

	}

	int x, y;
};

template<class T>
void Clamp(T low, T high, T &value)
{
	if(value < low) value = low;
	if(value > high) value = high;
}

float DegToRad(float a) { return a*0.01745329252f; };
float RadToDeg(float a) { return a*57.29577951f; };

// convert a point reference to an int position
// int func(Point &position2D, int width)  
int PointToIndexConversion(Point &point, int width, int height)
{
	Clamp(0,width,point.x);
	Clamp(0,height - 1, point.y);
	return point.x + (point.y * width);
}

typedef std::function<Point (Point &, float, float)> positionCalculationFunc;

Point UpdateFuncType1(Point &basePosition, float amount, float wavePosition)
{
	return Point(
		basePosition.x + 4 * (amount * ((sin(DegToRad(wavePosition)) + cos(DegToRad(2*wavePosition))) / 2.0f)),
		basePosition.y + (amount * ((cos(DegToRad(wavePosition)) * sin(DegToRad(wavePosition))) / 2.0f))
		);
}

Point UpdateFuncType2(Point &basePosition, float amount, float wavePosition)
{
	return Point(
		basePosition.x + 8 * (amount * ((sin(DegToRad(wavePosition)) + cos(DegToRad(4*wavePosition))) / 2.0f)),
		basePosition.y + 2 * (amount * ((cos(DegToRad(wavePosition)) * sin(DegToRad(wavePosition))) / 2.0f))
		);
}

Point UpdateFuncType3(Point &basePosition, float amount, float wavePosition)
{
	return Point(
		basePosition.x + 4 * (amount * ((sin(DegToRad(wavePosition)) + cos(DegToRad(wavePosition/1.5f))) / 2.0f)),
		basePosition.y + (amount * ((cos(DegToRad(wavePosition)) * sin(DegToRad(2*wavePosition))) / 2.0f))
		);
}

typedef std::function<int (Point &)> pointToIndexCalculationFunc;



struct PointAffinity
{
	PointAffinity(int side, Point &position, pointToIndexCalculationFunc &updateFunc, positionCalculationFunc &calc) : 
		side(side), position(0), basePosition(position), wavePosition(0.0f), amount(30.0f), incAmount(0.5f), updateFunc(updateFunc), calc(calc)
	{

	}
	PointAffinity(const PointAffinity& rhs) : 
		side(rhs.side), position(rhs.position), basePosition(rhs.basePosition), 
		wavePosition(rhs.wavePosition), amount(rhs.amount),incAmount(rhs.incAmount), updateFunc(rhs.updateFunc), calc(rhs.calc)
	{

	}

	bool operator<(PointAffinity &rhs)
	{
		return (rhs.position < position);
	}

	void UpdatePosition()
	{
		Point position2D = calc(basePosition, amount, wavePosition);
		wavePosition += incAmount;
		position = updateFunc(position2D);
	}

	pointToIndexCalculationFunc updateFunc;
	positionCalculationFunc calc;

	float wavePosition;
	float amount;
	float incAmount;
	int position;
	Point basePosition;
	int side;
};

std::vector<PointAffinity> dataPoints;
int maxTLMDataPoints = 20; // 20; 

int sourceSelect = 0;
int destSelect = 1;
void Update(int width, int height)
{
	HRESULT hr = S_OK;

	fmodSystem->getSpectrum(leftSpectrum,modValue[modSelection].size,0,FMOD_DSP_FFT_WINDOW_RECT);
	fmodSystem->getSpectrum(rightSpectrum,modValue[modSelection].size,0,FMOD_DSP_FFT_WINDOW_RECT);

	int bandlimitSelect = 0;
	float entry_hz_inc = (44100.0f/2.0f) / modValue[modSelection].size;
	float entry_hz = 0.0f;
	bands[0][bandlimitSelect] = 0.0f;
	bands[1][bandlimitSelect] = 0.0f;

	for(int i = 0; i < modValue[modSelection].size; ++i)
	{
		if(entry_hz > bandlimits[bandlimitSelect])
		{
			++bandlimitSelect;
			bands[0][bandlimitSelect] = 0.0f;
			bands[1][bandlimitSelect] = 0.0f;
		}

		bands[0][bandlimitSelect] += leftSpectrum[i];
		bands[1][bandlimitSelect] += rightSpectrum[i];

		entry_hz += entry_hz_inc;
	}

	// Update positions
	std::for_each(dataPoints.begin(), dataPoints.end(), std::mem_fun_ref(&PointAffinity::UpdatePosition));
}

void UpdateCompute(int width, int height, ID3D11DeviceContext* context)
{
	HRESULT hr = S_OK;

	D3D11_MAPPED_SUBRESOURCE resouce;
	V(context->Map(g_pTLMDriveDataBuffer,0,D3D11_MAP_WRITE_DISCARD,0,&resouce));

	TLMInputData * pData = reinterpret_cast<TLMInputData*>(resouce.pData);
	D3DXVECTOR4 white(1.0f, 1.0f, 1.0f, 1.0f);
	D3DXVECTOR4 red(1.0f, 0.0f, 0.0f, 1.0f);
	D3DXVECTOR4 blue(0.0f, 0.0f, 1.0f, 1.0f);
	D3DXVECTOR4 black(0.0f, 0.0f, 0.0f, 1.0f);

	float multiplier[] = { -1, 1};
	int i = 0;
	int j = 0;
	std::for_each(dataPoints.begin(), dataPoints.end(), [&](PointAffinity dataPoint)
		{
			pData[i].m_driveValue = bands[dataPoint.side][j] * multiplier[sourceSelect] * 7.0f;
			pData[i].position = dataPoint.position;
			i++;
			if(i % 2 == 0)
				j++;
		}	
	);

	context->Unmap(g_pTLMDriveDataBuffer,0);
	
	context->CSSetShader(g_pComputeShader,NULL, 0);
	// Same order as in the shader (?)
	ID3D11ShaderResourceView* sourceViews[] = { g_pTLMInput[sourceSelect], g_pTLMDriveData };
	context->CSSetShaderResources(0,2,sourceViews);
	ID3D11UnorderedAccessView* destViews[] = { g_pColourOutput, g_pTLMOutput[destSelect] };
	context->CSSetUnorderedAccessViews(0,2,destViews, (UINT*)(&destViews) );

	//context->Dispatch(1,width,1);
	context->Dispatch(tlmDispatch.x, tlmDispatch.y, 1);

	// Unbind resources for CS
	ID3D11UnorderedAccessView* ppUAViewNULL[] = { NULL, NULL };
	context->CSSetUnorderedAccessViews( 0, 2, ppUAViewNULL, (UINT*)(&destViews) );
	ID3D11ShaderResourceView* ppSRVNULL[] = { NULL, NULL };
	context->CSSetShaderResources( 0, 2, ppSRVNULL );
}

void Sync()
{
	std::swap(sourceSelect,destSelect);
}

void UpdateVisualiser(ID3D11DeviceContext* context)
{
	HRESULT hr = S_OK;

	context->IASetInputLayout( g_pVertexLayout11 );
	UINT Strides[1];
	UINT Offsets[1];
	ID3D11Buffer* pVB[1];
	pVB[0] = g_pVertexBuffer;
	Strides[0] = sizeof(QuadVertex);
	Offsets[0] = 0;
	context->IASetVertexBuffers( 0, 1,pVB, Strides, Offsets );

	// Set the buffer.
	context->IASetIndexBuffer( g_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0 );

	// Set the shaders
	context->VSSetShader( g_pVertexShader, NULL, 0 );
	context->PSSetShader( g_pPixelShader, NULL, 0 );

	// VS Per object
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	V( context->Map( g_pcbVSPerObject, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
	CB_VS_PER_OBJECT* pVSPerObject = ( CB_VS_PER_OBJECT* )MappedResource.pData;
	pVSPerObject->m_WorldViewProj = XMMatrixTranspose(projectMatrix);

	context->Unmap( g_pcbVSPerObject, 0 );
	context->VSSetConstantBuffers( g_iCBVSPerObjectBind, 1, &g_pcbVSPerObject );

	D3D11_PRIMITIVE_TOPOLOGY PrimType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	context->PSSetSamplers( 0, 1, &g_pSamLinear );
	context->IASetPrimitiveTopology( PrimType );

	context->PSSetShaderResources( 0, 1, &g_pQuadColourSource );		// bind our source texture

	context->DrawIndexed( 6, 0, 0 );	// 6 indices

	ID3D11ShaderResourceView* ppSRVNULL[] = { NULL, NULL };

	context->VSSetShaderResources( 0, 1, ppSRVNULL );
	context->PSSetShaderResources( 0, 1, ppSRVNULL );
}

void ClearCommand(ID3D11DeviceContext* context)
{
	float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f };
	context->ClearRenderTargetView(g_pRenderTargetView, ClearColor);
}

RendererCommand DeferredWrapper(const std::function<void (ID3D11DeviceContext *)> &func,ID3D11DeviceContext * context, int width, int height)
{
	SetupDeferredContext(context,width, height);
	func(context);

	ID3D11CommandList * command = NULL;
	context->FinishCommandList(FALSE, &command);
	return RendererCommand(DrawingCommand_Render, command, 0);
}

RendererCommand DeferredComputeWrapper(const std::function<void (ID3D11DeviceContext*)> &func, ID3D11DeviceContext * context)
{
	func(context);
	ID3D11CommandList * command = NULL;
	context->FinishCommandList(FALSE, &command);
	return RendererCommand(DrawingCommand_Render, command, 0);
}

struct Scheduler : public IScheduler
{
	Concurrency::task_group group;
	Concurrency::concurrent_vector<UpdateFuncType> taskVector;

	Scheduler()
	{

	};

	void ProcessTaskQueue(float time)
	{
		if(taskVector.empty())
			return;

		std::for_each(taskVector.begin(), taskVector.end(), [this,time](UpdateFuncType &task)
		{
			this->group.run(std::bind(task, time));
		});
		taskVector.clear();
		group.wait();
	};

	void QueueTask(const UpdateFuncType &updateFunc)
	{
		taskVector.push_back(updateFunc);
	};

};

std::ofstream logfile("alphalog.txt");

void ParticleFader(Shard::ParticleColours& colours, Shard::Age& currentAge, Shard::Age& MaxAge, float deltaTime)
{
	__m128 alpha = _mm_set1_ps(1.0f);
	__m128 multipier = _mm_div_ps(currentAge.age, MaxAge.age);
//	logfile << multipier.m128_f32[0] << std::endl;
	colours.a = _mm_mul_ps(multipier,alpha);
}

Shard::particleForce ParticleGravity(Shard::ParticlePosition &position, Shard::EmitterPosition&, float)
{
	Shard::particleForce force;
	for(int i = 0; i < 4; ++i)
	{
		force.x[i] = 0.0f;
		force.y[i] = -0.098f / 60.0f;
	}

	return force;
}

//TODO: Fix Window size to sane values

DWORD startTime = 0;

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

 	// TODO: Place code here.
	MSG msg = {0};
	HWND window;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_D3D11MTDXGITEST, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow, window))
	{
		return FALSE;
	}

	// Main message loop:
	// Old main loop disabled for now
	// Might want to restore to this when we enter the land of threads
	/*while (GetMessage(&msg, NULL, 0, 0))
	{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
	}*/

//	SetupD3D11(window);

	RECT rc;
	GetClientRect( window, &rc );
	UINT width = rc.right - rc.left;
	UINT height = rc.bottom - rc.top;

	

	Concurrency::unbounded_buffer<RendererCommand> commandList;
	Concurrency::unbounded_buffer<int> completionNotice;

	RenderingAgent renderer(commandList,completionNotice);
	renderer.SetupD3D(window, g_singleThread);
	if(!g_singleThread)
	{
		renderer.start();
		Concurrency::receive(completionNotice);
		g_pd3dDevice->CreateDeferredContext(0, &g_pDeferredContext);
		SetupDeferredContext(g_pDeferredContext, width, height);
		Concurrency::asend(commandList, RendererCommand(DrawingCommand_NOP, NULL, 0));
	}

	Shard::colourModifierCollection colourMods;
	colourMods.push_back(ParticleFader);
	Shard::positionModifierCollection positionMods;
//	positionMods.push_back(ParticleGravity);
	Shard::rotationModifierCollection rotationMods;
	Shard::DefaultColour defaultColour = { 1.0f, 1.0f, 1.0f, 1.0f};
												// life time in milliseconds therefore 16.6 * 60 = 1 second due to 16ms time steps in hardcoded use
#ifndef _DEBUG
	//Shard::EmitterDetails emitterDetails(1000000,250000,(16.6f * 60.0f * 2.0f),(16.6f*60.0f*6.0f), 0.015f, defaultColour, 1.0f, positionMods, colourMods, rotationMods);
	Shard::EmitterDetails emitterDetails(1000000,2500,(16.6f * 60.0f * 2.0f),(16.6f*60.0f*6.0f), 0.015f, defaultColour, 1.0f, positionMods, colourMods, rotationMods);
#else
	Shard::EmitterDetails emitterDetails(40000,10000,(16.6f * 60.0f * 2.0f),(16.6f*60.0f*6.0f), 0.007f, defaultColour, 1.0f, positionMods, colourMods, rotationMods);
#endif
	Shard::ParticleEmitter emitter(emitterDetails);
	Scheduler particleScheduler;

	RendererCommand present(DrawingCommand_Present, 0, 0);

	FMOD_RESULT result = FMOD::System_Create(&fmodSystem);
	ERRCHECK(result);

	result = fmodSystem->init(100,FMOD_INIT_NORMAL,0);
	ERRCHECK(result);

	FMOD::Sound *sound;
	result = fmodSystem->createStream("test.mp3",FMOD_LOOP_NORMAL | FMOD_2D | FMOD_SOFTWARE,0,&sound);
//	result = fmodSystem->createStream("test.s3m",FMOD_LOOP_NORMAL | FMOD_2D | FMOD_SOFTWARE,0,&sound);
	ERRCHECK(result);

	FMOD::Channel * channel;
//	result = fmodSystem->playSound(FMOD_CHANNEL_FREE, sound, false,&channel);

	// generate some locations


	int yborder = 60; // 30;							// how many rows between us and the border
	int yOffset = (height - (yborder*2))/10;   // how many rows between each object
	int xborder = 80;
	int xRange = (width - (xborder*2)) / 2;
	int xStep = xRange / 2;
	pointToIndexCalculationFunc updateFunc = std::bind(PointToIndexConversion, _1, width, height);
	positionCalculationFunc calcFuncs[] = { UpdateFuncType1, UpdateFuncType2, UpdateFuncType3 };
	int calcFuncsSize = sizeof(calcFuncs)/sizeof(calcFuncs[0]);
	for(int i = 0; i < maxTLMDataPoints/2; i++)
	{
		for(int j = 0; j < 2; j++)
		{
			Point position((width/2) + (((j == 0 )? 1 : -1) * (rand() % xStep)), yborder + (yOffset * i));
			dataPoints.push_back(PointAffinity(j, position, updateFunc, calcFuncs[rand() % calcFuncsSize]));
		}
	}

	std::sort(dataPoints.begin(), dataPoints.end());
	int counter = 0;
	float multiplier = 1.0f;
	std::for_each(dataPoints.begin(), dataPoints.end(), [&](PointAffinity &point)
	{
		point.incAmount *= multiplier;
		if(point.side == 0)
		{
			++counter;
		}
		if(counter % 2)
		{
			multiplier += 0.2f;
		}
	});

	// Single threaded update type message loop
	
	DWORD baseTime = timeGetTime();
	startTime = baseTime;

	bool fired = false;
	emitter.Trigger();
	while(WM_QUIT != msg.message)
	{
		if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if(msg.message == WM_LBUTTONUP)
			{
				int xPos = GET_X_LPARAM(msg.lParam); 
				int yPos = GET_Y_LPARAM(msg.lParam);
				xPos -= width/2;
				yPos -= height/2;
				yPos *= -1;
				emitter.Trigger(xPos, yPos);
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			DWORD newTime = timeGetTime();
			bool update = (newTime > baseTime + 16);
			
			if(update)
			{
				fmodSystem->update();
				Update(width, height);
				// Do the particle magic!
				emitter.PreUpdate(particleScheduler,16.0f);
				particleScheduler.ProcessTaskQueue(16.0f);
				emitter.PostUpdate();
			}
			if(!g_singleThread)
			{
				int numDrawCalls = Concurrency::receive(completionNotice);

				switch (g_SendMode)
				{
				case SendMode_FullDeferred:
					{
						if(update)
						{	
							RendererCommand computeCmd = DeferredComputeWrapper(std::bind(UpdateCompute, width, height,_1), g_pDeferredContext);
							Concurrency::send(commandList, computeCmd);
						}
						RendererCommand visualCmd = DeferredWrapper(std::bind(UpdateVisualiser, _1), g_pDeferredContext, width, height);
						RendererCommand clearCommand = DeferredWrapper(std::bind(ClearCommand, _1), g_pDeferredContext, width, height);
						RendererCommand particlePreRendercmd = DeferredWrapper(std::bind(&Shard::ParticleEmitter::PreRender, emitter, _1 ), g_pDeferredContext, width, height);
						RendererCommand particleRendercmd = DeferredWrapper(std::bind(&Shard::ParticleEmitter::Render, emitter, _1 ), g_pDeferredContext, width, height);
						Concurrency::send(commandList, clearCommand);

						Concurrency::send(commandList, visualCmd);
						Concurrency::send(commandList, particlePreRendercmd);
						Concurrency::send(commandList, particleRendercmd);
					}
					break;
				case SendMode_DirectSend:
					{
						Concurrency::send(commandList, RendererCommand(DrawingCommand_Function, std::bind(ClearCommand, _1), 1));
						Concurrency::send(commandList, RendererCommand(DrawingCommand_Function, std::bind(UpdateCompute, width, height, _1), 2));
						Concurrency::send(commandList, RendererCommand(DrawingCommand_Function, std::bind(UpdateVisualiser, _1), 3));
						Concurrency::send(commandList, RendererCommand(DrawingCommand_Function, std::bind(&Shard::ParticleEmitter::PreRender, &emitter, _1), 4));
						Concurrency::send(commandList, RendererCommand(DrawingCommand_Function, std::bind(&Shard::ParticleEmitter::Render, &emitter, _1 ), 5));
					}
					break;
				case SendMode_MixedSend:
					{
						
						Concurrency::send(commandList, RendererCommand(DrawingCommand_Function, std::bind(ClearCommand, _1), 1));
						RendererCommand computeCmd = DeferredComputeWrapper(std::bind(UpdateCompute, width, height,_1), g_pDeferredContext);
						RendererCommand visualCmd = DeferredWrapper(std::bind(UpdateVisualiser, _1), g_pDeferredContext, width, height);
						Concurrency::send(commandList, computeCmd);
						Concurrency::send(commandList, visualCmd);
					}
				}
				Concurrency::send(commandList, present);
			}
			else
			{
				ClearCommand(g_pImmediateContext);
				UpdateCompute(width, height,g_pImmediateContext);
				UpdateVisualiser(g_pImmediateContext);

				renderer.ProcessCommand(present);
			}
			//Concurrency::send(commandList, RendererCommand(EDrawingCommand_Waiting, NULL, newTime));
// 			{ 
// 				char error[256]; 
// 				sprintf_s(error, 256,"%u\t%u\n",baseTime, newTime); 
// 				OutputDebugStringA(error); 
// 			}
			if(update)
			{
				Sync();
				baseTime = newTime;
				
			}
			// DebugOutputInt(numDrawCalls);
		}
		SwitchToThread();
	}

	if(!g_singleThread)
	{
		RendererCommand quitCommand (DrawingCommand_Quit, 0, timeGetTime());
		Concurrency::send(commandList, quitCommand);

		Concurrency::agent::wait(&renderer);
	}
	else
	{
		ShutDownD3D();
	}

	sound->release();
	fmodSystem->release();
	
	return (int) msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_D3D11MTDXGITEST));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= NULL;
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, HWND &hWnd)
{
   hInst = hInstance; // Store instance handle in our global variable

   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

   if (!hWnd)
   {
      return FALSE;
   }

   RECT rect;
   GetWindowRect(hWnd, &rect);
   rect.top = 100;
   rect.left = 50;
   rect.bottom = rect.top + 1080;
   rect.right = rect.left + 1600;

   AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, 0, WS_EX_APPWINDOW);
   SetWindowPos(hWnd, HWND_BOTTOM, rect.top, rect.left, rect.right - rect.left, rect.bottom - rect.top, SWP_ASYNCWINDOWPOS);
  
   ShowWindow(hWnd, nCmdShow);


   
   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

HRESULT SetupD3D11(HWND target )
{
	HRESULT hr = S_OK;

	RECT rc;
	GetClientRect( target, &rc );
	UINT width = rc.right - rc.left;
	UINT height = rc.bottom - rc.top;

	UINT createDeviceFlags = 0; // D3D11_CREATE_DEVICE_SINGLETHREADED; // disable locks, enable deferred?
#ifdef _DEBUG
	createDeviceFlags |= D3D10_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = sizeof( driverTypes ) / sizeof( driverTypes[0] );

	D3D_FEATURE_LEVEL featureLevels[] = 
	{
		D3D_FEATURE_LEVEL_11_0
	};
	UINT numFeatureLevels = sizeof(featureLevels) / sizeof(featureLevels[0]);

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );
	sd.BufferCount = 1;
	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = target;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	for( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ )
	{
		g_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDeviceAndSwapChain( NULL, g_driverType, NULL, createDeviceFlags,featureLevels,numFeatureLevels,
			D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, NULL, &g_pImmediateContext);
		if( SUCCEEDED( hr ) )
			break;
	}
	if( FAILED( hr ) )
		return hr;

	// Create a render target view
	ID3D11Texture2D* pBuffer;
	hr = g_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), ( LPVOID* )&pBuffer );
	if( FAILED( hr ) )
		return hr;

	hr = g_pd3dDevice->CreateRenderTargetView( pBuffer, NULL, &g_pRenderTargetView );
	pBuffer->Release();
	if( FAILED( hr ) )
		return hr;

	g_pImmediateContext->OMSetRenderTargets( 1, &g_pRenderTargetView, NULL );

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = width;
	vp.Height = height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pImmediateContext->RSSetViewports( 1, &vp );

	D3D11_TEXTURE2D_DESC desc;
	pBuffer->GetDesc(&desc);
	SetupD3D11TestResources(g_pd3dDevice,&desc, width, height );

	return S_OK;
}

void ShutDownD3D()
{
	if( g_pDeferredContext ){ g_pDeferredContext->ClearState(); g_pDeferredContext->Flush(); }
	if( g_pImmediateContext ){ g_pImmediateContext->ClearState(); g_pImmediateContext->Flush(); }

	// Release compute shader views
	SAFE_RELEASE(g_pTLMInput[0]);
	SAFE_RELEASE(g_pTLMInput[1]);
	SAFE_RELEASE(g_pTLMOutput[0]);
	SAFE_RELEASE(g_pTLMOutput[1]);
	SAFE_RELEASE(g_pTLMDriveData);
	SAFE_RELEASE(g_pColourOutput);

	// Release buffers
	SAFE_RELEASE(g_pTLMMap[0]);
	SAFE_RELEASE(g_pTLMMap[1]);
	SAFE_RELEASE(g_pTLMDriveDataBuffer);
	SAFE_RELEASE(g_pColourOutTexure);

	// Release compute shader
	SAFE_RELEASE(g_pComputeShader);

	// Release vertex/pixel shader related data
	SAFE_RELEASE( g_pVertexLayout11 );
	SAFE_RELEASE( g_pVertexBuffer );
	SAFE_RELEASE( g_pIndexBuffer );
	SAFE_RELEASE( g_pVertexShader );
	SAFE_RELEASE( g_pPixelShader );
	SAFE_RELEASE( g_pSamLinear );

	SAFE_RELEASE( g_pcbVSPerObject );

	// final view release
	SAFE_RELEASE(g_pQuadColourSource);

	if( g_pRenderTargetView ) g_pRenderTargetView->Release();
	if( g_pSwapChain ) g_pSwapChain->Release();

	if( g_pDeferredContext ) { g_pDeferredContext->Release(); g_pDeferredContext = NULL; }
	if( g_pImmediateContext ){ g_pImmediateContext->Release(); g_pImmediateContext = NULL; }

#ifdef _DEBUG
	ID3D11Debug * debug;
	g_pd3dDevice->QueryInterface( IID_ID3D11Debug, (VOID**)(&debug) );
	debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
	debug->Release();
#endif
	if( g_pd3dDevice ) g_pd3dDevice->Release();

}

//--------------------------------------------------------------------------------------
// Create Structured Buffer on GPU
//--------------------------------------------------------------------------------------
HRESULT CreateStructuredBufferOnGPU( ID3D11Device* pDevice, UINT uElementSize, UINT uCount, VOID* pInitData, ID3D11Buffer** ppBufOut )
{
	*ppBufOut = NULL;

	D3D11_BUFFER_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.ByteWidth = uElementSize * uCount;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = uElementSize;

	if ( pInitData )
	{
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = pInitData;
		return pDevice->CreateBuffer( &desc, &InitData, ppBufOut );
	} else
		return pDevice->CreateBuffer( &desc, NULL, ppBufOut );
}

//--------------------------------------------------------------------------------------
// Create Structured Buffer on GPU which can be written to by the cpu
//--------------------------------------------------------------------------------------
HRESULT CreateDynamicStructuredBufferOnGPU( ID3D11Device* pDevice, UINT uElementSize, UINT uCount, VOID* pInitData, ID3D11Buffer** ppBufOut )
{
	*ppBufOut = NULL;

	D3D11_BUFFER_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;		// UNORDERED bind flag means this is writable by the GPU, which clashes with dynamic above
	desc.ByteWidth = uElementSize * uCount;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = uElementSize;

	if ( pInitData )
	{
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = pInitData;
		return pDevice->CreateBuffer( &desc, &InitData, ppBufOut );
	} else
		return pDevice->CreateBuffer( &desc, NULL, ppBufOut );
}

//--------------------------------------------------------------------------------------
// Create Texture on the GPU which can also be bound as a resource for the compute shader
//--------------------------------------------------------------------------------------
HRESULT CreateUnorderedAccessTextureBufferOnGPU( ID3D11Device* pDevice, DXGI_FORMAT format, UINT width, UINT height, VOID* pInitData, ID3D11Texture2D** ppTextureOut )
{
	*ppTextureOut = NULL;

	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = format;
	desc.Usage = D3D11_USAGE_DEFAULT;	// GPU read/write access
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.SampleDesc.Count = 1;
	if ( pInitData )
	{
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = pInitData;
		return pDevice->CreateTexture2D( &desc, &InitData, ppTextureOut );
	} else
		return pDevice->CreateTexture2D( &desc, NULL, ppTextureOut );
}

//--------------------------------------------------------------------------------------
// Create Shader Resource View for Structured or Raw Buffers
//--------------------------------------------------------------------------------------
HRESULT CreateBufferSRV( ID3D11Device* pDevice, ID3D11Buffer* pBuffer, ID3D11ShaderResourceView** ppSRVOut )
{
	D3D11_BUFFER_DESC descBuf;
	ZeroMemory( &descBuf, sizeof(descBuf) );
	pBuffer->GetDesc( &descBuf );

	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	desc.BufferEx.FirstElement = 0;

	if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS )
	{
		// This is a Raw Buffer

		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		desc.BufferEx.NumElements = descBuf.ByteWidth / 4;
	} else
		if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED )
		{
			// This is a Structured Buffer

			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
		} else
		{
			return E_INVALIDARG;
		}

		return pDevice->CreateShaderResourceView( pBuffer, &desc, ppSRVOut );
}

//--------------------------------------------------------------------------------------
// Create Unordered Access View for Structured or Raw Buffers
//-------------------------------------------------------------------------------------- 
HRESULT CreateBufferUAV( ID3D11Device* pDevice, ID3D11Buffer* pBuffer, ID3D11UnorderedAccessView** ppUAVOut )
{
	D3D11_BUFFER_DESC descBuf;
	ZeroMemory( &descBuf, sizeof(descBuf) );
	pBuffer->GetDesc( &descBuf );

	D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	desc.Buffer.FirstElement = 0;

	if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS )
	{
		// This is a Raw Buffer

		desc.Format = DXGI_FORMAT_R32_TYPELESS; // Format must be DXGI_FORMAT_R32_TYPELESS, when creating Raw Unordered Access View
		desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		desc.Buffer.NumElements = descBuf.ByteWidth / 4; 
	} 
	else if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED )
	{
		// This is a Structured Buffer

		desc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
		desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride; 
	}
	else
	{
		return E_INVALIDARG;
	}

	return pDevice->CreateUnorderedAccessView( pBuffer, &desc, ppUAVOut );
}

//--------------------------------------------------------------------------------------
// Create Unordered Access View for Texture Buffers
//-------------------------------------------------------------------------------------- 
HRESULT CreateTextureUAV( ID3D11Device* pDevice, ID3D11Texture2D* pBuffer, ID3D11UnorderedAccessView** ppUAVOut )
{
	D3D11_TEXTURE2D_DESC descBuf;
	ZeroMemory( &descBuf, sizeof(descBuf) );
	pBuffer->GetDesc( &descBuf );

	D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipSlice = 0;
	desc.Format = descBuf.Format;

	return pDevice->CreateUnorderedAccessView( pBuffer, &desc, ppUAVOut );
}
HRESULT CreateTextureView( ID3D11Device* pDevice, ID3D11Texture2D* pBuffer, ID3D11ShaderResourceView** ppSRVOut )
{
	D3D11_TEXTURE2D_DESC descBuf;
	ZeroMemory( &descBuf, sizeof(descBuf) );
	pBuffer->GetDesc( &descBuf );

	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipLevels = 1;
	desc.Texture2D.MostDetailedMip = 0;


	return pDevice->CreateShaderResourceView( pBuffer, &desc, ppSRVOut );
}

//--------------------------------------------------------------------------------------
// Compile and create the CS
//--------------------------------------------------------------------------------------
HRESULT CreateComputeShader( LPCWSTR pSrcFile, LPCSTR pFunctionName, LPCSTR pProfile, 
	ID3D11Device* pDevice, ID3D11ComputeShader** ppShaderOut, CONST D3D10_SHADER_MACRO * pDefines = NULL)
{
	HRESULT hr;

	// Finds the correct path for the shader file.
	// This is only required for this sample to be run correctly from within the Sample Browser,
	// in your own projects, these lines could be removed safely
	ID3DBlob* pErrorBlob = NULL;
	ID3DBlob* pBlob = NULL;
	UINT flags = D3D10_SHADER_ENABLE_STRICTNESS;
//#ifdef _DEBUG
	flags |= D3D10_SHADER_DEBUG;
//#endif
	hr = D3DX11CompileFromFile( pSrcFile, pDefines, NULL, pFunctionName, pProfile, flags, NULL, NULL, &pBlob, &pErrorBlob, NULL );
	if ( FAILED(hr) )
	{
		if ( pErrorBlob )
			OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );

		SAFE_RELEASE( pErrorBlob );
		SAFE_RELEASE( pBlob );    

		return hr;
	}    

	hr = pDevice->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, ppShaderOut );

	SAFE_RELEASE( pErrorBlob );
	SAFE_RELEASE( pBlob );

	return hr;
}

//--------------------------------------------------------------------------------------
// Use this until D3DX11 comes online and we get some compilation helpers
//--------------------------------------------------------------------------------------
HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
	HRESULT hr = S_OK;

	// find the file
	WCHAR *str = szFileName;

	// open the file
	HANDLE hFile = CreateFile( str, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN, NULL );
	if( INVALID_HANDLE_VALUE == hFile )
		return E_FAIL;

	// Get the file size
	LARGE_INTEGER FileSize;
	GetFileSizeEx( hFile, &FileSize );

	// create enough space for the file data
	BYTE* pFileData = new BYTE[ FileSize.LowPart ];
	if( !pFileData )
		return E_OUTOFMEMORY;

	// read the data in
	DWORD BytesRead;
	if( !ReadFile( hFile, pFileData, FileSize.LowPart, &BytesRead, NULL ) )
		return E_FAIL; 

	CloseHandle( hFile );

	// Compile the shader
	ID3DBlob* pErrorBlob;
	hr = D3DCompile( pFileData, FileSize.LowPart, "none", NULL, NULL, szEntryPoint, szShaderModel, D3D10_SHADER_ENABLE_STRICTNESS, 0, ppBlobOut, &pErrorBlob );

	delete []pFileData;

	if( FAILED(hr) )
	{
		OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
		SAFE_RELEASE( pErrorBlob );
		return hr;
	}
	SAFE_RELEASE( pErrorBlob );

	return S_OK;
}

// Setup D3D test resources
HRESULT SetupD3D11TestResources(ID3D11Device* pd3dDevice, const D3D11_TEXTURE2D_DESC* pBackBufferSurfaceDesc, int width, int height)
{
	//////////////////////////////////////////////////////////////////////////
	// Buffers etc required for the compute shader
	//////////////////////////////////////////////////////////////////////////
	const int bufferSize = (pBackBufferSurfaceDesc->Height*pBackBufferSurfaceDesc->Width);
	CreateStructuredBufferOnGPU(pd3dDevice,sizeof(TLMDataBuffer),bufferSize,NULL,&g_pTLMMap[0]);
	CreateStructuredBufferOnGPU(pd3dDevice, sizeof(TLMDataBuffer),bufferSize,NULL,&g_pTLMMap[1]);
	// needs to be updatable by the cpu
	CreateDynamicStructuredBufferOnGPU(pd3dDevice, sizeof(TLMInputData),20,NULL,&g_pTLMDriveDataBuffer);
	// Render output buffer
	CreateUnorderedAccessTextureBufferOnGPU(pd3dDevice,DXGI_FORMAT_R16G16B16A16_FLOAT,pBackBufferSurfaceDesc->Width,pBackBufferSurfaceDesc->Height,NULL,&g_pColourOutTexure);

	// Setup the compute shader resource views
	CreateBufferSRV(pd3dDevice,g_pTLMMap[0],&g_pTLMInput[0]);
	CreateBufferSRV(pd3dDevice,g_pTLMMap[1],&g_pTLMInput[1]);
	CreateBufferSRV(pd3dDevice,g_pTLMDriveDataBuffer,&g_pTLMDriveData);
	CreateBufferUAV(pd3dDevice,g_pTLMMap[0],&g_pTLMOutput[0]);
	CreateBufferUAV(pd3dDevice,g_pTLMMap[1],&g_pTLMOutput[1]);
	CreateTextureUAV(pd3dDevice,g_pColourOutTexure,&g_pColourOutput);
	CreateTextureView(pd3dDevice,g_pColourOutTexure, &g_pQuadColourSource);

	D3D11_MAPPED_SUBRESOURCE resouce;
	ID3D11DeviceContext * context;
	pd3dDevice->GetImmediateContext(&context);
	context->Map(g_pTLMDriveDataBuffer,0,D3D11_MAP_WRITE_DISCARD,0,&resouce);

	TLMInputData * pData = reinterpret_cast<TLMInputData*>(resouce.pData);
	D3DXVECTOR4 black(0.0f, 1.0f, 0.0f, 1.0f);

	for(int i = 0; i < maxTLMDataPoints; i++)
	{
		pData[i].m_driveValue = 0.0f;
		pData[i].position = 0;
	//	pData[i].m_colour = black;
	}

	context->Unmap(g_pTLMDriveDataBuffer,0);
	SAFE_RELEASE(context);

	// Create shaders
	D3D10_SHADER_MACRO defines[6];
	char bufferWidth[256];
	sprintf_s(bufferWidth, 256, "%i",width);
	char bufferHeight[256];
	sprintf_s(bufferHeight, 256, "%i",height);
	const char * TLMWidth = "32";
	tlmDispatch.x = ceil(float(width)/32.0f);
	const char *TLMHeight = "32";
	tlmDispatch.y = ceil(float(height)/32.0f);
	char tlm_dataPoints[256];
	sprintf_s(tlm_dataPoints, 256, "%i", maxTLMDataPoints);

	defines[0].Name = "BUFFER_WIDTH";
	defines[0].Definition = bufferWidth;
	defines[1].Name = "BUFFER_HEIGHT";
	defines[1].Definition = bufferHeight;
	defines[2].Name = "TLM_WIDTH";
	defines[2].Definition = TLMWidth;
	defines[3].Name = "TLM_HEIGHT";
	defines[3].Definition = TLMHeight;
	defines[4].Name = "TLM_DATAPOINTS";
	defines[4].Definition = tlm_dataPoints;
	defines[5].Name = NULL;
	defines[5].Definition = NULL;

	CreateComputeShader(L"TLMCompute.hlsl","TLMMain","cs_5_0",pd3dDevice,&g_pComputeShader,defines);

	// Setup shaders etc
	HRESULT hr;
	projectMatrix = XMMatrixOrthographicLH(pBackBufferSurfaceDesc->Width,pBackBufferSurfaceDesc->Height,1,10);

	ID3DBlob* pVertexShaderBuffer = NULL;
	ID3DBlob* pPixelShaderBuffer = NULL;
	V_RETURN(CompileShaderFromFile(L"2DOutputShader_VS.hlsl","VSMain", "vs_5_0", &pVertexShaderBuffer));
	V_RETURN(CompileShaderFromFile(L"2DOutputShader_PS.hlsl","PSMain", "ps_5_0", &pPixelShaderBuffer));

	// Create the shaders
	V_RETURN( pd3dDevice->CreateVertexShader( pVertexShaderBuffer->GetBufferPointer(), pVertexShaderBuffer->GetBufferSize(), NULL, &g_pVertexShader ) );
	V_RETURN( pd3dDevice->CreatePixelShader( pPixelShaderBuffer->GetBufferPointer(), pPixelShaderBuffer->GetBufferSize(), NULL, &g_pPixelShader ) );

	// Create our vertex input layout
	const D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	V_RETURN( pd3dDevice->CreateInputLayout( layout, ARRAYSIZE( layout ), pVertexShaderBuffer->GetBufferPointer(), pVertexShaderBuffer->GetBufferSize(), &g_pVertexLayout11 ) );

	SAFE_RELEASE( pVertexShaderBuffer );
	SAFE_RELEASE( pPixelShaderBuffer );

	// Create a sampler state
	D3D11_SAMPLER_DESC SamDesc;
	SamDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SamDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	SamDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	SamDesc.MipLODBias = 0.0f;
	SamDesc.MaxAnisotropy = 1;
	SamDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	SamDesc.BorderColor[0] = SamDesc.BorderColor[1] = SamDesc.BorderColor[2] = SamDesc.BorderColor[3] = 0;
	SamDesc.MinLOD = 0;
	SamDesc.MaxLOD = D3D11_FLOAT32_MAX;
	V_RETURN( pd3dDevice->CreateSamplerState( &SamDesc, &g_pSamLinear ) );

	{
		// Setup constant buffers
		D3D11_BUFFER_DESC Desc;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.MiscFlags = 0;

		Desc.ByteWidth = sizeof( CB_VS_PER_OBJECT );
		V_RETURN( pd3dDevice->CreateBuffer( &Desc, NULL, &g_pcbVSPerObject ) );
	}

	// Create a basic mesh
	float halfWidth = float(width)/2.0f;
	float halfHeight = float(height)/2.0f;
	QuadVertex mesh[] = 
	{
		D3DXVECTOR3(-halfWidth, -halfHeight, 5.0f), D3DXVECTOR2(1.0f, 1.0f),
		D3DXVECTOR3(-halfWidth, halfHeight, 5.0f), D3DXVECTOR2(1.0f, 0.0f),
		D3DXVECTOR3(halfWidth, halfHeight, 5.0f), D3DXVECTOR2(0.0f, 0.0f),
		D3DXVECTOR3(halfWidth, -halfHeight, 5.0f), D3DXVECTOR2(0.0f, 1.0f)
	};

	{
		// Fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.Usage            = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth        = sizeof( QuadVertex ) * 4;
		bufferDesc.BindFlags        = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags   = 0;
		bufferDesc.MiscFlags        = 0;

		// Fill in the subresource data.
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = mesh;
		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;

		// Create the vertex buffer.
		V_RETURN(pd3dDevice->CreateBuffer( &bufferDesc, &InitData, &g_pVertexBuffer ));
	}

	{
		// Create indices.
		unsigned int indices[] = { 0, 1, 2, 0, 2, 3 };

		// Fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.Usage           = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth       = sizeof( unsigned int ) * 6;
		bufferDesc.BindFlags       = D3D11_BIND_INDEX_BUFFER;
		bufferDesc.CPUAccessFlags  = 0;
		bufferDesc.MiscFlags       = 0;

		// Define the resource data.
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = indices;
		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;

		// Create the buffer with the device.
		V_RETURN(pd3dDevice->CreateBuffer( &bufferDesc, &InitData, &g_pIndexBuffer ));
	}
	return S_OK;
}

void SetupDeferredContext(ID3D11DeviceContext* context, int width, int height )
{
	context->OMSetRenderTargets( 1, &g_pRenderTargetView, NULL );

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = width;
	vp.Height = height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	context->RSSetViewports( 1, &vp );
}
