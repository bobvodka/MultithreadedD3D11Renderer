//--------------------------------------------------------------------------------------
// Globals
//--------------------------------------------------------------------------------------
/*cbuffer cbPerObject : register( b0 )
{
	matrix		g_mWorldViewProjection	: packoffset( c0 );
//	matrix		g_mWorld				: packoffset( c4 );
};
*/
//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
	float2 vPosition	: POSITION;
	float4 vColour	: COLOUR0;
};

struct GS_INPUT
{
	float4 vColour	: COLOUR0;
	float2 vPosition	: POSITION;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
GS_INPUT VSMain( VS_INPUT Input )
{
	GS_INPUT Output;
	
	Output.vPosition = Input.vPosition;
	Output.vColour = Input.vColour;
	
	return Output;
}