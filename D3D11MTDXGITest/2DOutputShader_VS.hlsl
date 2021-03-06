//--------------------------------------------------------------------------------------
// Globals
//--------------------------------------------------------------------------------------
cbuffer cbPerObject : register( b0 )
{
	matrix		g_mWorldViewProjection	: packoffset( c0 );
//	matrix		g_mWorld				: packoffset( c4 );
};

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
	float4 vPosition	: POSITION;
	float2 vTexcoord	: TEXCOORD0;
};

struct VS_OUTPUT
{
	float2 vTexcoord	: TEXCOORD0;
	float4 vPosition	: SV_POSITION;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VS_OUTPUT VSMain( VS_INPUT Input )
{
	VS_OUTPUT Output;
	
	Output.vPosition = mul( Input.vPosition, g_mWorldViewProjection );
	Output.vTexcoord = Input.vTexcoord;
	
	return Output;
}