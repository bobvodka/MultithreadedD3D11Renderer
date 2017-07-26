#pragma once

#include <functional>
#include <vector>
#include <intrin.h>

namespace Shard
{
	struct ParticlePosition
	{
		ParticlePosition(__m128 & xPositions, __m128 & yPositions) : xPositions(xPositions), yPositions(yPositions)
		{

		}

		ParticlePosition(const ParticlePosition& rhs) : xPositions(rhs.xPositions), yPositions(rhs.yPositions)
		{

		}

		__m128 & xPositions;
		__m128 & yPositions;
	};

	struct ParticleColours
	{
		ParticleColours(__m128 &red, __m128 &green, __m128 &blue, __m128 &alpha) :  r(red), g(green), b(blue), a(alpha)
		{

		}

		ParticleColours(const ParticleColours &rhs) : r(rhs.r), g(rhs.g), b(rhs.b), a(rhs.a)
		{

		}

		__m128 &r;
		__m128 &g; 
		__m128 &b; 
		__m128 &a;
	};

	struct particleForce
	{
		float x[4];
		float y[4];
	};

	struct Age
	{
		Age(__m128 &age) : age(age)
		{

		}

		Age(const Age &rhs) : age(rhs.age)
		{

		}

		__m128 &age;
	};

	struct particleRotations
	{
		float rotation[4];
	};

	struct EmitterPosition 
	{
		EmitterPosition() : x(0.0f), y(0.0f)
		{

		}
		EmitterPosition(float x, float y) : x(x), y(y)
		{

		}
		EmitterPosition(const EmitterPosition &rhs) : x(rhs.x), y(rhs.y)
		{

		}
		float x, y;
	};

	// Affecter hook functors
	// particleForce (ParticlePosition &Positions, float timeDelta)
	typedef std::function<particleForce (ParticlePosition &, EmitterPosition&, float )> particlePositionModifierFunc;
	// void (ParticleColours &colours, Age &ages, Age& maxAge, float timeDelta)
	typedef std::function<void (ParticleColours&, Age&, Age&, float)> particleColourModifierFunc;
	//particleRotations (float timeDelta)
	typedef std::function<particleRotations (float)> particleRotationModiferFunc;

	typedef std::vector<particlePositionModifierFunc> positionModifierCollection;
	typedef std::vector<particleColourModifierFunc> colourModifierCollection;
	typedef std::vector<particleRotationModiferFunc> rotationModifierCollection;
}