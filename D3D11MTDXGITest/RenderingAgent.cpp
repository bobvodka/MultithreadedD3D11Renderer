#include "StdAfx.h"
#include "RenderingAgent.h"

#include <d3d11.h>
#include <d3dx11.h>

extern ID3D11DeviceContext*	g_pImmediateContext;
extern IDXGISwapChain*      g_pSwapChain;

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

HRESULT SetupD3D11(HWND);
void ShutDownD3D();

RenderingAgent::RenderingAgent(Concurrency::ISource<RendererCommand>& commandList, Concurrency::ITarget<int>& completionNotice) : 
	commandList(commandList), completionNotice(completionNotice), shouldQuit(false)
{
}


RenderingAgent::~RenderingAgent()
{
}

void RenderingAgent::run()
{
	// Should probably signal this as an error
	if(singleThread)
	{
		done();
	}

	SetupD3D11(window);
	Concurrency::send(completionNotice, 1);

	shouldQuit = false;
	numDrawCalls = 0;

	while(!shouldQuit)
	{
		RendererCommand command = Concurrency::receive(commandList);
		ProcessCommand(command);
	}


	RendererCommand command;
	while(try_receive(commandList, command))
	{
		if(command.cmd)
		{
				command.cmd->Release();
		}
	}

	ShutDownD3D();
	done();
}

void RenderingAgent::ProcessCommand(const RendererCommand &command)
{
	HRESULT res = S_OK;
	switch(command.cmdID)
	{
	case DrawingCommand_Quit:
		shouldQuit = true;
		break;
	case DrawingCommand_Present:
		{
			res = g_pSwapChain->Present(1, 0);
			if(!singleThread) Concurrency::asend(completionNotice, numDrawCalls);
			numDrawCalls = 0;
		}
		break;
	case DrawingCommand_Function:
		{
			command.deferredFunction(g_pImmediateContext);
			numDrawCalls++;
		}
		break;
	case DrawingCommand_Render:
		if(command.cmd)
		{
			g_pImmediateContext->ExecuteCommandList(command.cmd, FALSE);
			command.cmd->Release();
			numDrawCalls++;
		}
		break;
	case DrawingCommand_NOP:
		{
			if(!singleThread) Concurrency::asend(completionNotice, 1);
		}
		break;
	case DrawingCommand_Waiting:
		break;
	default:
		shouldQuit = shouldQuit;
		break;
	}
}

void RenderingAgent::SetupD3D(HWND window, bool singleThreadSetup)
{
	this->window = window;
	if(singleThreadSetup)
	{
		SetupD3D11(window);
	}
	singleThread = singleThreadSetup;
}

RendererCommand::RendererCommand(DrawingCommandType cmdID, ID3D11CommandList * cmd, DWORD time) : cmdID(cmdID), cmd(cmd), time(time)
{
	if(cmd) { cmd->AddRef(); }
}

RendererCommand::RendererCommand(DrawingCommandType cmdID, const RenderFunction &deferredFunction, DWORD time) : cmdID(cmdID), cmd(NULL), deferredFunction(deferredFunction), time(time)
{

}

RendererCommand::RendererCommand(const RendererCommand &rhs) : cmdID(rhs.cmdID), cmd(rhs.cmd), deferredFunction(rhs.deferredFunction), time(rhs.time)
{
	if(cmd) { cmd->AddRef(); }
}

RendererCommand::~RendererCommand()
{
	if(cmd) { cmd->Release(); cmd = NULL; };
}