#pragma once

#include "EmitterHookDetails.h"

#include <queue>
#include <random>

/////////////////////////////////////////////////////////////////////////////////
// This D3D11 stuff is temporary until we get a decent renderable system together
////////////////////////////////////////////////////////////////////////////////
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11GeometryShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11BlendState;
#include <Xnamath.h>
//////////////////////////////////////////////////////////////////////////
// End Temp
//////////////////////////////////////////////////////////////////////////

struct IScheduler;

namespace Shard
{
	struct ParticleInfomation 
	{
		float * __restrict x;
		float * __restrict y;
		float * __restrict scale;
		float * __restrict momentumX;
		float * __restrict momentumY;
		float * __restrict velocityX;
		float * __restrict velocityY;
		float * __restrict age;
		float * __restrict maxAge;
		float * __restrict colourR;
		float * __restrict colourG;
		float * __restrict colourB;
		float * __restrict colourA;
		float * __restrict rotation;

	private:
		volatile long * refCount;
		void IncRefCount();
		void DecRefCount();
	public:
		ParticleInfomation();
		ParticleInfomation(const ParticleInfomation &rhs) ;
		ParticleInfomation(ParticleInfomation &&rhs) ;
		~ParticleInfomation();

		void ReserveMemory(int size);
	};

	struct DefaultColour
	{
		float r, b, g, a;
	};
	
	struct EmitterDetails
	{
		EmitterDetails(int maxParticleCount, int releaseAmount, float minLife, float maxLife, float releaseSpeed, DefaultColour colour, float scale,
			const positionModifierCollection &positionModFuncs, const colourModifierCollection &colourModFuncs, const rotationModifierCollection &rotationModFuncs);

		EmitterDetails(const EmitterDetails &rhs);

		int maxParticleCount;
		int releaseAmount;
		float maxLife;
		float minLife;
		float releaseSpeed;
		DefaultColour colour;
		float scale;

		positionModifierCollection positionModFuncs;
		colourModifierCollection colourModFuncs;
		rotationModifierCollection rotationModFuncs;

	private:
		void Swap(EmitterDetails &details);

	};

	class RandomNumberLUT
	{
	public:
		RandomNumberLUT(int count, float rndVal1, float rndVal2);

		void Generate();

		float operator()(int index);
	private:
		typedef std::ranlux3_01 RandomGeneratorEngine;
		RandomGeneratorEngine rndNumGenEngine;
		//typedef std::cauchy_distribution<float> RandomGenerator;  // this generator, combined with settings for rndNumGen in constructor give a kinda spiked explosion effect with a few fast moving particles
		typedef std::normal_distribution<float> RandomGenerator;  // this generator gives a more compact particle effect when combined with settings in the constructor
		RandomGenerator rndNumGen;
		int numberCount;
		float * __restrict numbers;
	};

	class ParticleEmitter
	{
	public:
		ParticleEmitter(const EmitterDetails& details);
		ParticleEmitter(const ParticleEmitter& rhs);
		~ParticleEmitter();

		void UpdatePosition(float x, float y);
		void UpdatePosition(float position[2]);
		void UpdatePosition(const EmitterPosition &position);

		void PreUpdate(IScheduler &taskQueue, float deltaTime);
		void PostUpdate();
		void Update(float deltaTime, int blockStart, int blockEnd);

		void Trigger(); // Emits
		void Trigger(float x, float y);
		void Trigger(float position[2]);
		void Trigger(const EmitterPosition &position);

		void PreRender(ID3D11DeviceContext *);
		void Render(ID3D11DeviceContext *);

	private:
		int EmitParticles(IScheduler &taskQueue, const EmitterPosition &position);
		void SetParticleData(int amount, float value, float * __restrict location);
		void GenerateForces(int amount, float * __restrict locationX, float * __restrict locationY);
		void GenerateAges(int amount, float * __restrict lifeLocation, float * __restrict maxLifeLocation);

		void UpdateAges( int start, int offset, bool remain, float deltaTime );
		void UpdateRotations( int offset, int len, float deltaTime );
		void UpdateColours( int offset, int len, float deltaTime );
		void UpdatePositionsAndMomentums( int offset, int len, float deltaTime );

		void MoveParticleBlock(int start, int len, int dest);
		void MoveParticleBlockWithoutAge(int start, int len, int dest);

		void Swap(ParticleEmitter &rhs);

		int usedParticles;
		EmitterDetails details;
		ParticleInfomation particleData;
		EmitterPosition position;

		std::queue<EmitterPosition> queuedTriggers;

		//std::ranlux3 rndNumGen;
		//typedef std::linear_congruential_engine<UINT64,6364136223846793005,1442695040888963407,18446744073709551616> MMIX;
		//typedef std::knuth_b RandomGenerator;
		//typedef std::ranlux48_base RandomGeneratorEngine;
		typedef std::ranlux3_01 RandomGeneratorEngine;
		RandomGeneratorEngine rndNumGenEngine;
		//typedef std::cauchy_distribution<float> RandomGenerator;  // this generator, combined with settings for rndNumGen in constructor give a kinda spiked explosion effect with a few fast moving particles
		typedef std::normal_distribution<float> RandomGenerator;  // this generator gives a more compact particle effect when combined with settings in the constructor
		RandomGenerator rndNumGen;
		RandomGenerator lifeNumGen;


		//
		// Temp rendering things
		//

		ID3D11Buffer * emitterVertexData;
		int emitterVertexDataMaxCount;
		ID3D11VertexShader* vertexShader;
		ID3D11PixelShader*  pixelShader;
		ID3D11GeometryShader * geoShader;
		ID3D11InputLayout* vertexLayout;
		ID3D11Buffer* GSPerObject;
		XMMATRIX * projectMatrix;

		ID3D11BlendState * blendState;
	};


}
