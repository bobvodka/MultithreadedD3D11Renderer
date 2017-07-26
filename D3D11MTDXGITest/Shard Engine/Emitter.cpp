#include "StdAfx.h"
#include "Emitter.h"

#include <malloc.h>
#include <memory>
#include <random>

#include <intrin.h>

#include <assert.h>

#include <ppl.h>

#include "..\IScheduler.h"

/// Temp logging stuff
#include <fstream>
extern std::ofstream logfile;

/////////////////////////////////////////////////////////////////////////////////
// This D3D11 stuff is temporary until we get a decent renderable system together
////////////////////////////////////////////////////////////////////////////////
#include <D3D11.h>
#include <d3dCompiler.h>
#include <Xnamath.h>

//#include <d3d9.h>

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

HRESULT WINAPI DXUTTrace( const CHAR* strFile, DWORD dwLine, HRESULT hr,
	const WCHAR* strMsg, bool bPopMsgBox );
HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut );

struct ParticleVertexBufferType
{
	XMFLOAT2 position;
	XMFLOAT4 colour;
//	XMCOLOR colour;
//	float scale;
};
extern ID3D11Device*                       g_pd3dDevice;

struct CB_GS_PER_OBJECT
{
	XMMATRIX m_WorldViewProj;
	XMFLOAT4 m_positions[4];
	//	D3DXMATRIX m_World;
};

//////////////////////////////////////////////////////////////////////////
// End Temp
//////////////////////////////////////////////////////////////////////////

#pragma intrinsic (_InterlockedIncrement)

using namespace std::placeholders;



namespace Shard
{
	// http://en.wikipedia.org/wiki/File:Normal_Distribution_PDF.svg
	// http://en.wikipedia.org/wiki/Cauchy_distribution

	RandomNumberLUT * rndNumTable;
	RandomNumberLUT * rndLifeTable;

	ParticleEmitter::ParticleEmitter(const EmitterDetails& details) : details(details), usedParticles(0), position(0.0f, 0.0f)
//			,rndNumGen(-0.5 * 3.14159265358979, 0.5 * 3.14159265358979), lifeNumGen(-details.minLife, (details.maxLife - details.minLife))
//			,rndNumGen(0.0f, 0.5f), lifeNumGen((details.maxLife - details.minLife)/details.maxLife, 0.2f)
			,rndNumGen(0.0f, 5.0f), lifeNumGen((details.maxLife - details.minLife)/details.maxLife, 0.2f)
	{
		// Ensure things are multiple of 4
		this->details.maxParticleCount += this->details.maxParticleCount % 4;
		this->details.releaseAmount += this->details.releaseAmount % 4;

		int memorySize = this->details.maxParticleCount * sizeof(float);

		particleData.ReserveMemory(memorySize);
		rndNumGenEngine.seed();
		rndNumTable = new RandomNumberLUT(2000000, 0.0f, 5.0f);
		rndNumTable->Generate();
		rndLifeTable = new RandomNumberLUT(1000000,(details.maxLife - details.minLife)/details.maxLife, 0.2f );
		rndLifeTable->Generate();
		// Create some initial buffers for data
/*		if(this->details.releaseAmount == this->details.maxParticleCount)
		{
			emitterVertexDataMaxCount = this->details.releaseAmount;
		}
		else
		{
			emitterVertexDataMaxCount = this->details.releaseAmount * 4;	// yay! magic numbers! :|
		}*/

		emitterVertexDataMaxCount = this->details.maxParticleCount;

		D3D11_BUFFER_DESC vbDesc;
		vbDesc.ByteWidth = sizeof(ParticleVertexBufferType) * emitterVertexDataMaxCount;
		vbDesc.Usage = D3D11_USAGE_DYNAMIC;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		vbDesc.MiscFlags = 0;
		vbDesc.StructureByteStride = 0;

		HRESULT hr = g_pd3dDevice->CreateBuffer(&vbDesc, nullptr, &emitterVertexData);

		// Grab the shaders (this would normally live in a material but as we don't have a system yet)
		ID3DBlob* pVertexShaderBuffer = NULL;
		ID3DBlob* pPixelShaderBuffer = NULL;
		ID3DBlob* pGeoShaderBuffer = NULL;

		V(CompileShaderFromFile(L"EmitterShader_VS.hlsl","VSMain", "vs_5_0", &pVertexShaderBuffer));
		V(CompileShaderFromFile(L"EmitterShader_PS.hlsl","PSMain", "ps_5_0", &pPixelShaderBuffer));
		V(CompileShaderFromFile(L"EmitterShader_GS.hlsl","GSMain", "gs_5_0", &pGeoShaderBuffer));

		// Create the shaders
		V( g_pd3dDevice->CreateVertexShader( pVertexShaderBuffer->GetBufferPointer(), pVertexShaderBuffer->GetBufferSize(), NULL, &vertexShader ) );
		V( g_pd3dDevice->CreatePixelShader( pPixelShaderBuffer->GetBufferPointer(), pPixelShaderBuffer->GetBufferSize(), NULL, &pixelShader ) );
		V( g_pd3dDevice->CreateGeometryShader( pGeoShaderBuffer->GetBufferPointer(), pGeoShaderBuffer->GetBufferSize(), NULL, &geoShader ) );

		
		// Create our vertex input layout
		const D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION",  0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOUR",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, sizeof(XMFLOAT2), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		//	{ "SCALE", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		V( g_pd3dDevice->CreateInputLayout( layout, ARRAYSIZE( layout ), pVertexShaderBuffer->GetBufferPointer(), pVertexShaderBuffer->GetBufferSize(), &vertexLayout ) );

		SAFE_RELEASE( pVertexShaderBuffer );
		SAFE_RELEASE( pPixelShaderBuffer );
		SAFE_RELEASE( pGeoShaderBuffer );

		void * matrixSpace = _aligned_malloc(sizeof(XMMATRIX), 16);
		projectMatrix =  new(matrixSpace) XMMATRIX();
		*projectMatrix = XMMatrixOrthographicLH(1600,1080,1,10);

		// Setup constant buffers
		D3D11_BUFFER_DESC Desc;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.MiscFlags = 0;

		Desc.ByteWidth = sizeof( CB_GS_PER_OBJECT );
		V( g_pd3dDevice->CreateBuffer( &Desc, NULL, &GSPerObject ) );

		// Setup blend state
		// Alpha blending of particles
		D3D11_BLEND_DESC blendDesc;
		ZeroMemory(&blendDesc, sizeof(D3D11_BLEND_DESC));
		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; 
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		g_pd3dDevice->CreateBlendState(&blendDesc, &blendState);

	}
	ParticleEmitter::ParticleEmitter(const ParticleEmitter& rhs) : 
		details(rhs.details), usedParticles(rhs.usedParticles), particleData(rhs.particleData), position(rhs.position), queuedTriggers(rhs.queuedTriggers),
			emitterVertexData(rhs.emitterVertexData), emitterVertexDataMaxCount(rhs.emitterVertexDataMaxCount), vertexShader(rhs.vertexShader),
			pixelShader(rhs.pixelShader), geoShader(rhs.geoShader),vertexLayout(rhs.vertexLayout),GSPerObject(rhs.GSPerObject),blendState(rhs.blendState)

	{
		emitterVertexData->AddRef();
		vertexShader->AddRef();
		pixelShader->AddRef();
		geoShader->AddRef();
		vertexLayout->AddRef();
		GSPerObject->AddRef();
		blendState->AddRef();
		void * matrixSpace = _aligned_malloc(sizeof(XMMATRIX), 16);
		projectMatrix =  new(matrixSpace) XMMATRIX();
		*projectMatrix = *rhs.projectMatrix;
	}

	ParticleEmitter::~ParticleEmitter()
	{
		SAFE_RELEASE(emitterVertexData);
		
		SAFE_RELEASE(vertexShader);
		SAFE_RELEASE(pixelShader);
		SAFE_RELEASE(geoShader);
		SAFE_RELEASE(vertexLayout);
		SAFE_RELEASE(GSPerObject);
		SAFE_RELEASE(blendState);

		projectMatrix->~XMMATRIX();
		_aligned_free((void*)projectMatrix);
	}

	void ParticleEmitter::UpdatePosition(float x, float y)
	{
		position = EmitterPosition(x, y);
	}
	void ParticleEmitter::UpdatePosition(float newPosition[2])
	{
		position = EmitterPosition(newPosition[0], newPosition[1]);
	}
	void ParticleEmitter::UpdatePosition(const EmitterPosition &newPosition)
	{
		position = newPosition;
	}

	void ParticleEmitter::PreUpdate(IScheduler &taskQueue, float deltaTime)
	{
		// Services any required trigger calls
		while(!queuedTriggers.empty())
		{
			const EmitterPosition & position = queuedTriggers.front();
			if(details.maxParticleCount > usedParticles) 
			{
				usedParticles += EmitParticles(taskQueue, position);
			}
			queuedTriggers.pop();
		}

		// then work out block sizes and schedule those blocks for updates
		// blocks are based on multiples of cache line sizes (currently hard coded to i7 64byte L1 cache line size) 
		// The number is based on the fact each cache line can hold 16 floats (or 4 particle groups) so we are processing 32 batches of them
		// These numbers probably need to be investigated to see what kind of granularity we can use taking into account cache alising etc
		if (usedParticles > 0)
		{
			int start = 0;
			int adjustedUsed = usedParticles/4;
// 			while(start < adjustedUsed)
// 			{
// 				int size = (start + 256 > adjustedUsed) ? adjustedUsed - start : 256;
// 				taskQueue.QueueTask(std::bind(&ParticleEmitter::Update, this, _1, start, start + size));
// 				start += size;
// 			}
			taskQueue.QueueTask(std::bind(&ParticleEmitter::Update, this, _1, 0, adjustedUsed));
		}
	}
#ifndef OLDMETHOD
	void ParticleEmitter::PostUpdate()
	{
		int adjustedUsedParticles = usedParticles/4;
		for(int i = 0; i < adjustedUsedParticles;)
		{
			if(particleData.age[i] < 0.0f)
			{
				if(i < adjustedUsedParticles - 1)
				{
					particleData.age[i] = particleData.age[adjustedUsedParticles-1];
					particleData.maxAge[i] = particleData.maxAge[adjustedUsedParticles -1];
					int tmpi = i;
					i *=4;
					for(int j = 0; j < 4; ++j)
					{
						particleData.colourA[i+j] = particleData.colourA[usedParticles - 4 + j];
						particleData.colourR[i+j] = particleData.colourR[usedParticles - 4 + j];
						particleData.colourG[i+j] = particleData.colourG[usedParticles - 4 + j];
						particleData.colourB[i+j] = particleData.colourB[usedParticles - 4 + j];
						particleData.momentumX[i+j] = particleData.momentumX[usedParticles - 4 + j];
						particleData.momentumY[i+j] = particleData.momentumY[usedParticles - 4 + j];
						particleData.rotation[i+j] = particleData.rotation[usedParticles - 4 + j];
						particleData.scale[i+j] = particleData.scale[usedParticles - 4 + j];
						particleData.velocityX[i+j] = particleData.velocityX[usedParticles - 4 + j];
						particleData.velocityY[i+j] = particleData.velocityY[usedParticles - 4 + j];
						particleData.x[i+j] = particleData.x[usedParticles - 4 + j];
						particleData.y[i+j] = particleData.y[usedParticles - 4 + j];
					}
					i = tmpi;
				}
				--adjustedUsedParticles;
				usedParticles -= 4;
			}
			else
			{
				++i;
			}
		}
	}
#else

	void ParticleEmitter::PostUpdate()
	{
		// Retire particles as needed
		// As we enforce block of 4 emitting we can process in blocks of 4
		
		// The plan!
		// Search for a the first 'dead' pixel block
		// - if not found then bail out
		// Search for the end of that block
		// - if no end found then subtract 'start' from used and bail
		// - if end found then move through particles until we find a dead one (back copying the ages as we go)
		// -- when we do mark the position then
		// --- copy data for every other data block down to that location
		// --- update count for used particles
		// --- note location for 'start'
		// repeat loop from step 2 to find end of block

		if(usedParticles == 0)
			return;
		
		int idx = 0;
		// Search for first dead block of particles
		int adjustedUsedParticles = usedParticles/4;
		while(idx < adjustedUsedParticles && particleData.age[idx] > 0.0f)
		{	
			++idx;			
		}
		// - if not found then bail out
		if(idx >= adjustedUsedParticles)
		{
			return;
		}

		int deadBlockStart = idx;
		int particlesRemoved = 0;
		while(idx < adjustedUsedParticles)
		{
			// Now search for end of block
			while(idx < adjustedUsedParticles && particleData.age[idx] <= 0.0f)
			{
				++idx;
			}

			// if not found then everything from start to end was dead so reduce count and bail
			if(idx >= adjustedUsedParticles)
			{
				particlesRemoved += (adjustedUsedParticles - deadBlockStart);
				break;
			}
			// tag where we go to for the 'alive' block
			int aliveBlockStart = idx;
			int movedParticleDest = deadBlockStart;
			// now search for the end of this block of 'alive' particles
			// moving blocks of 'ages' down as we go
			while(idx < adjustedUsedParticles && particleData.age[idx] > 0.0f)
			{
				particleData.age[movedParticleDest] = particleData.age[idx];
// 				float * start = particleData.age + idx;
// 				float * end = start + 1;
// 				float * dest = particleData.age + movedParticleDest;
// 				std::move(start, end, dest);
				++idx;
				++movedParticleDest;
			}
			// copy 
			//int aliveBlockLength = movedParticleDest - aliveBlockStart;  // check logic - this returned a len of -8 with moveParticleDest = 88, aliveBlockStart = 96, deadBlockStart = 0, usedParticles = 4888
			int aliveBlockLength = movedParticleDest - deadBlockStart;
			particlesRemoved += (aliveBlockStart - deadBlockStart);
			MoveParticleBlockWithoutAge(aliveBlockStart, aliveBlockLength, deadBlockStart);
			
			// At this point we re-loop starting from 'idx' which now points at a dead block
			// we also move the 'start' up the amount we have just copied so we are now
			// passed the end of the data we have just copied down which gives us the destination for
			// our next move downwards.
			deadBlockStart += aliveBlockLength;
		}

		// Finally reset used particle count
		usedParticles -= (particlesRemoved*4);
	}
#endif
	//TODO: check over the logic for the particle system as the above code was returning some strange age numbers within blocks which were expected to hold the same lifetime

	void ParticleEmitter::Update(float deltaTime, int blockStart, int blockEnd)
	{
		// Update a chunk of particles here
		int len = (blockEnd) - (blockStart);
		int remain = len % 8;
		int offset = (blockEnd) - remain;
		bool hasRemain = remain > 0;

		assert(blockEnd <= details.maxParticleCount);

		// Deal with age subtraction first
		UpdateAges(blockStart, offset, hasRemain, deltaTime);
		// Next momentum += velocity; pos += momentum * deltaTime;
		UpdatePositionsAndMomentums(blockStart, blockEnd, deltaTime);
		if(!details.colourModFuncs.empty())
		{
			UpdateColours(blockStart, blockEnd, deltaTime);
		}
		if(!details.rotationModFuncs.empty())
		{
			UpdateRotations(blockStart, blockEnd, deltaTime);
		}
	}

	// Note: final system needs double buffering
	//       for that reason this function would need a 'blend' amount to be passed in
	//		 we would then 'blend' between the two sets of data
	//		 -- what to do about new particles spawned at 'new' time which don't exist in old? - ugh. Whole system will need a rejig for double buffering tbh
	void ParticleEmitter::PreRender(ID3D11DeviceContext *context)
	{
		if(usedParticles == 0)
			return; 

		// Map buffer and upload data to vertex buffer
		HRESULT hr;
		D3D11_MAPPED_SUBRESOURCE resource;
		V(context->Map(emitterVertexData,0,D3D11_MAP_WRITE_DISCARD,0,&resource));

		ParticleVertexBufferType * data = reinterpret_cast<ParticleVertexBufferType*>(resource.pData);
		int dataCount = usedParticles <= emitterVertexDataMaxCount ? usedParticles : emitterVertexDataMaxCount; // temp until we handle resizing
		/*for(int i = 0; i < dataCount; ++i)
		{
			data[i].position.x = particleData.x[i];
			data[i].position.y = particleData.y[i];
			data[i].colour.x = particleData.colourR[i];
			data[i].colour.y = particleData.colourG[i];
			data[i].colour.z = particleData.colourB[i];
			data[i].colour.w = particleData.colourA[i];
	//		data[i].scale = particleData.scale[i];
		}*/
		Concurrency::parallel_for(size_t(0), size_t(dataCount), [&,this](size_t i) 
			{
				data[i].position.x = this->particleData.x[i];
				data[i].position.y = this->particleData.y[i];
				data[i].colour.x = this->particleData.colourR[i];
				data[i].colour.y = this->particleData.colourG[i];
				data[i].colour.z = this->particleData.colourB[i];
				data[i].colour.w = this->particleData.colourA[i];
		});
		context->Unmap(emitterVertexData,0);

		D3D11_MAPPED_SUBRESOURCE MappedResource;
		V( context->Map( GSPerObject, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
		CB_GS_PER_OBJECT* pVSPerObject = ( CB_GS_PER_OBJECT* )MappedResource.pData;
		pVSPerObject->m_WorldViewProj = XMMatrixTranspose(*projectMatrix);
		// Correct widing order for a quad
		pVSPerObject->m_positions[0] = XMFLOAT4(-1.0f,  1.0f, 0.0f, 0.0f);
		pVSPerObject->m_positions[1] = XMFLOAT4( 1.0f,  1.0f, 0.0f, 0.0f);
		pVSPerObject->m_positions[2] = XMFLOAT4(-1.0f, -1.0f, 0.0f, 0.0f);
		pVSPerObject->m_positions[3] = XMFLOAT4( 1.0f, -1.0f, 0.0f, 0.0f);
		context->Unmap( GSPerObject, 0 );
	}

	void ParticleEmitter::Render(ID3D11DeviceContext *context)
	{
		if(usedParticles == 0)
			return; 
		//D3DPERF_BeginEvent(0xFF0000FF, L"Particle::Render");
		// Setup input
		context->IASetInputLayout(vertexLayout);
		D3D11_PRIMITIVE_TOPOLOGY PrimType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		context->IASetPrimitiveTopology( PrimType );

		// Setup VB bindings
		UINT Strides[1];
		UINT Offsets[1];
		ID3D11Buffer* pVB[1];
		pVB[0] = emitterVertexData;
		Strides[0] = sizeof(ParticleVertexBufferType);
		Offsets[0] = 0;
		context->IASetVertexBuffers( 0, 1,pVB, Strides, Offsets );
		
		// Setup shaders
		context->VSSetShader(vertexShader, nullptr, 0);
		context->PSSetShader(pixelShader, nullptr, 0);
		context->GSSetShader(geoShader, nullptr, 0);

		// Setup constant buffers
		context->GSSetConstantBuffers(0, 1, &GSPerObject);

		// Setup blending
		float blendfactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		UINT sampleMask = 0xffffffff;

		context->OMSetBlendState(blendState,blendfactor,sampleMask);

		// Render
		int dataCount = usedParticles <= emitterVertexDataMaxCount ? usedParticles : emitterVertexDataMaxCount; // temp until we handle resizing
		//context->Draw(1, 0);
		context->Draw(dataCount, 0);
		//D3DPERF_EndEvent();

		context->VSSetShader(nullptr, nullptr, 0);
		context->PSSetShader(nullptr, nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);		

		context->OMSetBlendState(nullptr,blendfactor,sampleMask);
	}

	void ParticleEmitter::Trigger()
	{
		queuedTriggers.push(EmitterPosition());
	}

	void ParticleEmitter::Trigger(float x, float y)
	{
		queuedTriggers.push(EmitterPosition(x,y));
	}

	void ParticleEmitter::Trigger(float position[2])
	{
		queuedTriggers.push(EmitterPosition(position[0], position[1]));
	}

	void ParticleEmitter::Trigger(const EmitterPosition &position)
	{
		queuedTriggers.push(position);
	}

	void ParticleEmitter::Swap(ParticleEmitter &rhs)
	{

	}

	int ParticleEmitter::EmitParticles(IScheduler &taskQueue, const EmitterPosition &position)
	{
		int realReleaseAmount = (usedParticles + details.releaseAmount > details.maxParticleCount ) ? details.maxParticleCount - usedParticles : details.releaseAmount;
		
		SetParticleData(realReleaseAmount, 0.0f, particleData.momentumX + usedParticles);
		SetParticleData(realReleaseAmount, 0.0f, particleData.momentumY + usedParticles);

		SetParticleData(realReleaseAmount, position.x, particleData.x + usedParticles);
		SetParticleData(realReleaseAmount, position.y, particleData.y + usedParticles);
	//	SetParticleData(realReleaseAmount, details.maxLife, particleData.age + usedParticles);
		SetParticleData(realReleaseAmount, details.scale, particleData.scale + usedParticles);
		SetParticleData(realReleaseAmount, details.colour.r, particleData.colourR + usedParticles);
		SetParticleData(realReleaseAmount, details.colour.g, particleData.colourG + usedParticles);
		SetParticleData(realReleaseAmount, details.colour.b, particleData.colourB + usedParticles);
		SetParticleData(realReleaseAmount, details.colour.a, particleData.colourA + usedParticles);
		
		// These two functions below are totally killing the spawn time for 1mill particles :(
		// looks like it's the random number generators
		GenerateForces(realReleaseAmount, particleData.velocityX + usedParticles, particleData.velocityY + usedParticles);

		GenerateAges(realReleaseAmount/4,particleData.age + (usedParticles/4), particleData.maxAge + (usedParticles/4) );

		return realReleaseAmount;
	}

	void ParticleEmitter::SetParticleData(int amount, float value, float * __restrict location)
	{
		//std::fill_n(location, amount, value);
		Concurrency::parallel_for(size_t(0), size_t(amount), [&](size_t i)
		{
			location[i] = value;
		});
	}

	void ParticleEmitter::GenerateForces(int amount, float * __restrict locationX, float * __restrict locationY)
	{
		//std::knuth_b::result_type min = std::knuth_b::min();
		//std::knuth_b::result_type max = std::knuth_b::max();
	//	RandomGenerator::result_type min = rndNumGen.min(); //RandomGenerator::min();
	//	RandomGenerator::result_type max = rndNumGen.max();// RandomGenerator::max();
	//	long int halfMax = max/2;

	//	std::generate_n(locationX, amount, [this,min,max]()->float { double initVal = double(this->rndNumGen()); float val = float((initVal-min)/max); return sin(val * this->details.releaseSpeed); });
	//	std::generate_n(locationY, amount, [this,min,max]()->float { double initVal = double(this->rndNumGen()); float val = float((initVal-min)/max); return cos(val * this->details.releaseSpeed); });
	
	//	std::generate_n(locationX, amount, [this]() -> float { float val = this->rndNumGen(rndNumGenEngine); return tan(val * this->details.releaseSpeed); });
	//	std::generate_n(locationY, amount, [this]() -> float { float val = this->rndNumGen(rndNumGenEngine); return tan(val * this->details.releaseSpeed); });

		Concurrency::parallel_for(size_t(0), size_t(amount), [this,amount, locationX,locationY](size_t i)
		{
			float val = (*rndNumTable)(i);// this->rndNumGen(rndNumGenEngine); 
			locationX[i] = (val * this->details.releaseSpeed); 
			val = (*rndNumTable)(i + amount);
			// val = this->rndNumGen(rndNumGenEngine); 
			locationY[i] = (val * this->details.releaseSpeed); 
		});
	}

	void ParticleEmitter::GenerateAges(int amount, float * __restrict lifeLocation, float * __restrict maxLifeLocation)
	{
		/*std::generate_n(maxLifeLocation, amount, [this,&lifeLocation]() -> float
		{ 
			float multiplier = abs(this->lifeNumGen(rndNumGenEngine));
			assert(multiplier > 0);
			float life = multiplier * this->details.maxLife;
			*lifeLocation = life;
			++lifeLocation;
			return life;
		});*/
		Concurrency::parallel_for(size_t(0), size_t(amount),size_t(1), [this,lifeLocation,maxLifeLocation](size_t i)
		{
			float multiplier = abs((*rndLifeTable)(i*2)); // abs(this->lifeNumGen(rndNumGenEngine));
			float life = multiplier * this->details.maxLife;
			lifeLocation[i] = life;
			maxLifeLocation[i] = life;		
		});
	}

	//Note: We might want to rearrange this so that we perform all the mods + write backs for all particles first
	//THEN re-read the data and perform the update. This might be slightly more cache/processor friendly in the long run
	//profile later. (Same applies for later colour and rotation adjustments)
	void ParticleEmitter::UpdatePositionsAndMomentums( int offset, int len, float deltaTime )
	{
		offset *= 4;
		len *= 4;
		for(int i = offset; i < len; i+= 4)
		{	
			__m128 posX = _mm_load_ps(particleData.x + i);
			__m128 posY = _mm_load_ps(particleData.y + i);
			__m128 velX = _mm_load_ps(particleData.velocityX + i);
			__m128 velY = _mm_load_ps(particleData.velocityY + i);
			if(!details.positionModFuncs.empty())
			{
				ParticlePosition position(posX, posY);
				std::for_each(details.positionModFuncs.begin(), details.positionModFuncs.end(), [&](particlePositionModifierFunc &func)
				{
					particleForce force = func(position, this->position, deltaTime);
					__m128 forceX = _mm_loadu_ps(force.x);
					velX = _mm_add_ps(velX, forceX);
					__m128 forceY = _mm_loadu_ps(force.y);
					velY = _mm_add_ps(velY, forceY);
				});
			}

			__m128 momentumsX = _mm_load_ps(particleData.momentumX + i);
			__m128 momentumsY = _mm_load_ps(particleData.momentumY + i);

			momentumsX = _mm_add_ps(momentumsX, velX);
			momentumsY = _mm_add_ps(momentumsY, velY);
			velX = _mm_setzero_ps();
			velY = _mm_setzero_ps();

			// store Velocity and momentum and reload position which should still live in cache

			__m128 time = _mm_load1_ps(&deltaTime);
			_mm_stream_ps(particleData.velocityX + i,velX);
			_mm_stream_ps(particleData.velocityY + i,velY);
			_mm_stream_ps(particleData.momentumX + i,momentumsX);
			_mm_stream_ps(particleData.momentumY + i,momentumsY);

			momentumsX = _mm_mul_ps(momentumsX, time);
			momentumsY = _mm_mul_ps(momentumsY, time);

			posX = _mm_add_ps(momentumsX, posX);
			posY = _mm_add_ps(momentumsY, posY);

			_mm_store_ps(particleData.x + i, posX);
			_mm_store_ps(particleData.y + i, posY);
		}
	}

	void ParticleEmitter::UpdateColours( int offset, int len, float deltaTime )
	{
		for(int i = (offset*4), agePos = offset; agePos < len; i+= 4, ++agePos)
		{
			__m128 age = _mm_load1_ps(particleData.age + agePos);
			__m128 maxage = _mm_load1_ps(particleData.maxAge + agePos);
			__m128 red = _mm_load_ps(particleData.colourR + i);
			__m128 green = _mm_load_ps(particleData.colourG + i);
			__m128 blue = _mm_load_ps(particleData.colourB + i);
			__m128 alpha = _mm_load_ps(particleData.colourA + i);

			Age ages(age);
			Age maxAges(maxage);
			ParticleColours colours(red, green, blue, alpha);
//			logfile << agePos <<  ",";
			std::for_each(details.colourModFuncs.begin(), details.colourModFuncs.end(), [&](particleColourModifierFunc &func)
			{			
				func(colours, ages, maxAges, deltaTime);
			});

			_mm_store_ps(particleData.colourR + i, red);
			_mm_store_ps(particleData.colourG + i, green);
			_mm_store_ps(particleData.colourB + i, blue);
			_mm_store_ps(particleData.colourA + i, alpha);
		}
	}

	void ParticleEmitter::UpdateRotations( int offset, int len, float deltaTime )
	{
		offset *= 4;
		len *= 4;
		for(int i = offset; i < len; i+= 4)
		{
			__m128 rotation = _mm_load_ps(particleData.rotation + i);
			std::for_each(details.rotationModFuncs.begin(), details.rotationModFuncs.end(), [&rotation, &deltaTime](particleRotationModiferFunc &func)
			{
				particleRotations rot = func(deltaTime);
				__m128 accumRotations = _mm_loadu_ps(rot.rotation);
				rotation = _mm_add_ps(rotation, accumRotations);
			});

			_mm_store_ps(particleData.rotation + i, rotation);
		}
	}

	void ParticleEmitter::UpdateAges( int start, int offset, bool remain, float deltaTime )
	{
		__m128 time = _mm_load1_ps(&deltaTime);

		for(int i = start; i < offset; i+= 8)
		{
			__m128 ages = _mm_load_ps(particleData.age + i);
			__m128 updatedAges = _mm_sub_ps(ages, time);
			__m128 ages2 = _mm_load_ps(particleData.age + i + 4);
			__m128 updatedAges2 = _mm_sub_ps(ages2, time);
			_mm_store_ps(particleData.age + i,updatedAges);
			_mm_store_ps(particleData.age + i + 4,updatedAges2);
		}

		if(remain)
		{
			__m128 ages = _mm_load_ps(particleData.age + offset );
			__m128 updatedAges = _mm_sub_ps(ages, time);
			_mm_store_ps(particleData.age + offset,updatedAges);
		}
		
	}

	void ParticleMover(float * start, float *end, float *dest)
	{
		while(start < end)
		{
			*dest++ = *start++;
		}
	}

	void ParticleEmitter::MoveParticleBlockWithoutAge(int start, int len, int dest)
	{
		//std::move(&particleData.maxAge[start], &particleData.maxAge[start + len], &particleData.maxAge[dest]);
		ParticleMover(&particleData.maxAge[start], &particleData.maxAge[start + len], &particleData.maxAge[dest]);
		
		// transform into 'real' block space from index space
		start *= 4;
		dest *= 4;
		len *= 4;
		assert(dest <= details.maxParticleCount);
/*		std::move(&particleData.x[start],&particleData.x[start+len],&particleData.x[dest]);
		std::move(&particleData.y[start],&particleData.y[start+len],&particleData.y[dest]);
		std::move(&particleData.scale[start],&particleData.scale[start+len],&particleData.scale[dest]);
		std::move(&particleData.momentumX[start],&particleData.momentumX[start+len],&particleData.momentumX[dest]);
		std::move(&particleData.momentumY[start],&particleData.momentumY[start+len],&particleData.momentumY[dest]);
		std::move(&particleData.velocityX[start],&particleData.velocityX[start+len],&particleData.velocityX[dest]);
		std::move(&particleData.velocityY[start],&particleData.velocityY[start+len],&particleData.velocityY[dest]);
		std::move(&particleData.colourR[start],&particleData.colourR[start+len],&particleData.colourR[dest]);
		std::move(&particleData.colourG[start],&particleData.colourG[start+len],&particleData.colourG[dest]);
		std::move(&particleData.colourB[start],&particleData.colourB[start+len],&particleData.colourB[dest]);
		std::move(&particleData.colourA[start],&particleData.colourA[start+len],&particleData.colourA[dest]);
		std::move(&particleData.rotation[start],&particleData.rotation[start+len],&particleData.rotation[dest]);
		*/
		ParticleMover(&particleData.x[start],&particleData.x[start+len],&particleData.x[dest]);
		ParticleMover(&particleData.y[start],&particleData.y[start+len],&particleData.y[dest]);
		ParticleMover(&particleData.scale[start],&particleData.scale[start+len],&particleData.scale[dest]);
		ParticleMover(&particleData.momentumX[start],&particleData.momentumX[start+len],&particleData.momentumX[dest]);
		ParticleMover(&particleData.momentumY[start],&particleData.momentumY[start+len],&particleData.momentumY[dest]);
		ParticleMover(&particleData.velocityX[start],&particleData.velocityX[start+len],&particleData.velocityX[dest]);
		ParticleMover(&particleData.velocityY[start],&particleData.velocityY[start+len],&particleData.velocityY[dest]);
		ParticleMover(&particleData.colourR[start],&particleData.colourR[start+len],&particleData.colourR[dest]);
		ParticleMover(&particleData.colourG[start],&particleData.colourG[start+len],&particleData.colourG[dest]);
		ParticleMover(&particleData.colourB[start],&particleData.colourB[start+len],&particleData.colourB[dest]);
		ParticleMover(&particleData.colourA[start],&particleData.colourA[start+len],&particleData.colourA[dest]);
		ParticleMover(&particleData.rotation[start],&particleData.rotation[start+len],&particleData.rotation[dest]);

		
	}

	void ParticleEmitter::MoveParticleBlock(int start, int len, int dest)
	{
		assert(dest <= details.maxParticleCount);
	//	std::move(&particleData.age[start],&particleData.age[start+len],&particleData.age[dest]);
		ParticleMover(&particleData.age[start],&particleData.age[start+len],&particleData.age[dest]);
		MoveParticleBlockWithoutAge(start, len, dest);
	}


	// Emitter details
	EmitterDetails::EmitterDetails(int maxParticleCount, int releaseAmount, float minLife, float maxLife, float releaseSpeed, DefaultColour colour, float scale, 
		const positionModifierCollection &positionModFuncs, const colourModifierCollection &colourModFuncs, const rotationModifierCollection &rotationModFuncs)
		: maxParticleCount(maxParticleCount), releaseAmount(releaseAmount), minLife(minLife), maxLife(maxLife), releaseSpeed(releaseSpeed), colour(colour), 
		positionModFuncs(positionModFuncs), colourModFuncs(colourModFuncs), rotationModFuncs(rotationModFuncs)
	{

	}

	EmitterDetails::EmitterDetails(const EmitterDetails &rhs)
	: maxParticleCount(rhs.maxParticleCount), minLife(rhs.minLife), maxLife(rhs.maxLife), releaseAmount(rhs.releaseAmount), releaseSpeed(rhs.releaseSpeed), colour(rhs.colour), scale(rhs.scale),
		positionModFuncs(rhs.positionModFuncs), colourModFuncs(rhs.colourModFuncs), rotationModFuncs(rhs.rotationModFuncs)
	{

	}

	void EmitterDetails::Swap(EmitterDetails &details)
	{

	}

	// Particle details

	ParticleInfomation::ParticleInfomation() : refCount(reinterpret_cast<long *>(_aligned_malloc(sizeof(int), 4)))
	{
		IncRefCount();
	};

	void ParticleInfomation::ReserveMemory(int memorySize)
	{
		int ageSize = std::max(memorySize/4, 4);
		age = reinterpret_cast<float*>(_aligned_malloc(ageSize, 16));		// only need 1/4 the memory size as each block has the same age
		maxAge = reinterpret_cast<float*>(_aligned_malloc(ageSize, 16));	// only need 1/4 the memory size as each block has the same age
		momentumX = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		momentumY = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		x = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		y = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		velocityX = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		velocityY = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		colourR = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		colourG = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		colourB = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		colourA = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));		
		rotation = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
		scale = reinterpret_cast<float*>(_aligned_malloc(memorySize, 16));
	}

	ParticleInfomation::ParticleInfomation(const ParticleInfomation &rhs) 
		: x(rhs.x), y(rhs.y), scale(rhs.scale), momentumX(rhs.momentumX), momentumY(rhs.momentumY), velocityX(rhs.velocityX), velocityY(rhs.velocityY), age(rhs.age), maxAge(rhs.maxAge),  
		colourR(rhs.colourR), colourG(rhs.colourG), colourB(rhs.colourB), colourA(rhs.colourA), rotation(rhs.rotation), refCount(rhs.refCount)
	{
		IncRefCount();
	};

	ParticleInfomation::ParticleInfomation(ParticleInfomation &&rhs)
		: x(rhs.x), y(rhs.y), scale(rhs.scale), momentumX(rhs.momentumX), momentumY(rhs.momentumY),  velocityX(rhs.velocityX), velocityY(rhs.velocityY), age(rhs.age), maxAge(rhs.maxAge),
		colourR(rhs.colourR), colourG(rhs.colourG), colourB(rhs.colourB), colourA(rhs.colourA), rotation(rhs.rotation), refCount(rhs.refCount)
	{
		rhs.x = NULL;
		rhs.y = NULL; 
		rhs.scale = NULL;
		rhs.momentumX = NULL;
		rhs.momentumY = NULL;
		rhs.velocityX = NULL;
		rhs.velocityY = NULL;
		rhs.age = NULL;
		rhs.maxAge = NULL;
		rhs.colourR = NULL;
		rhs.colourG = NULL;
		rhs.colourB = NULL;
		rhs.colourA = NULL;
		rhs.rotation = NULL;
		rhs.refCount = NULL;
	}
	ParticleInfomation::~ParticleInfomation()
	{
		if(refCount != NULL)
		{
			DecRefCount();
			if(*refCount == 0)
			{
				_aligned_free(x);
				_aligned_free(y);
				_aligned_free(scale);
				_aligned_free(momentumX);
				_aligned_free(momentumY);
				_aligned_free(velocityX);
				_aligned_free(velocityY);
				_aligned_free(age);
				_aligned_free(maxAge);
				_aligned_free(colourR);
				_aligned_free(colourG);
				_aligned_free(colourB);
				_aligned_free(colourA);
				_aligned_free(rotation);
				_aligned_free((void*)(refCount));
			}
		}
	}

	void ParticleInfomation::IncRefCount()
	{
		_InterlockedIncrement(refCount);
	}

	void ParticleInfomation::DecRefCount()
	{
		_InterlockedDecrement(refCount);
	}


	RandomNumberLUT::RandomNumberLUT(int count, float rndVal1, float rndVal2) : numberCount(count), numbers(nullptr), rndNumGen(rndVal1, rndVal2)
	{
	}

	void RandomNumberLUT::Generate()
	{
		if(numbers)
		{
			delete[] numbers;
		}
		numbers = new float[numberCount];
		rndNumGenEngine.seed();
		for(int i = 0; i < numberCount; ++i)
		{
			numbers[i] = rndNumGen(rndNumGenEngine);
		}
// 		Concurrency::parallel_for(size_t(0),size_t(numberCount), [this](size_t i)
// 		{
// 			this->numbers[i] = this->rndNumGen(this->rndNumGenEngine);
// 		});
	}

	float RandomNumberLUT::operator()(int index)
	{
		int realIndex = index % numberCount;
		return numbers[realIndex];
	}
}