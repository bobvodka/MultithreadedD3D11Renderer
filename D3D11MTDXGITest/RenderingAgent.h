#pragma once
#include <agents.h>

#include <functional>

enum DrawingCommandType
{
	DrawingCommand_Render,
	DrawingCommand_Function,
	DrawingCommand_Present,
	DrawingCommand_Waiting,
	DrawingCommand_NOP,
	DrawingCommand_Quit,
	
	DrawingCommand_MaxCommandTypes
};

struct ID3D11CommandList;
struct ID3D11DeviceContext;

typedef std::function<void (ID3D11DeviceContext*)> RenderFunction;

struct RendererCommand
{
	RendererCommand() : cmdID(DrawingCommand_NOP), cmd(NULL), time(0) {};
	RendererCommand(DrawingCommandType cmdID, ID3D11CommandList * cmd, DWORD time);
	RendererCommand(DrawingCommandType cmdID, const RenderFunction &deferredFunction, DWORD time);
	RendererCommand(const RendererCommand &rhs);
	~RendererCommand();

	DrawingCommandType cmdID;
	ID3D11CommandList * cmd;
	RenderFunction deferredFunction;
	DWORD time;
};

class RenderingAgent : public Concurrency::agent
{
public:
	RenderingAgent(Concurrency::ISource<RendererCommand>& commandList, Concurrency::ITarget<int> &completionNotice);
	virtual ~RenderingAgent();

	void SetupD3D(HWND window, bool singleThread);
	void ProcessCommand(const RendererCommand &command);
protected:
	void run();
private:
	Concurrency::ISource<RendererCommand> &commandList;
	Concurrency::ITarget<int> &completionNotice;

	HWND window;
	bool shouldQuit;
	int numDrawCalls;
	bool singleThread;
};

