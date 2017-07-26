# Multi-threaded Agent Base D3D11 Renderer Test

### About

This was a test project for a threaded/agent based D3D11 renderer using 'commands' sent from a main thread and executed on a rendering thread.

As this was also a D3D11 deferred context test is could operate in 3 modes

1. Single threaded where the renderer would execute commands directly
2. Multithreaded direct where the commands would directly execute on the D3D11 context in the rendering thread
3. Multithreaded indirect where the commands were recorded in to a CommandList for later execution

It was also possible to mix command types, with one example sending a draw command and then bundling two other commands in to a CommandList for later execution.

### Concepts

The renderer could execute, as noted, in both single and multi-threaded mode.

In single threaded mode commands are executed directly on a global *immediate context* before a 'present' command is sent to the renderer.

In multi-threaded mode things are a little more interesting.

The system is based upon *Agents*, in this case a single *Rendering Agent* is used.

Communication is carried out via two *unbound lists* which allow data to be written by one thread and read, in a blocking or async manner, by another.

In this case one thread is used to send command packets to the renderer while the other is a simple return status pipe to let the main thread block when waiting on the renderer to do something; line 585 in the D3D11MTDXGITest.cpp show this in action to block until the renderer has confirmed it has started up.

The main loop is simple a case of updating a few buffers/states and pushing command packets in to the buffer; the renderer then reads those same packets and executes the basic commands contained within - see RenderingAgent.cpp.

### Notes

This project probably doesn't run and hasn't been touched for some time (probably around 2010).

It makes use of C++11 features, including std::function and std::bind in the main loop.

It requires fmod for sound play back - the sound is used to drive a wave simultion (refered to as TLM) and give the GPU something to do. 
(The wave simulation is based on some updated code from my degree disertation entitled Transmission Line Matrix Theory On The GPU which should be in another repro.)

There is also the skeleton of a CPU particle system called **Shard** in there; I don't know if that works at all.

Code is licensed under MIT - see the license file for details.
