//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
//Texture2D	g_txDiffuse : register( t0 );
//SamplerState g_samLinear : register( s0 );

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct GS_OUTPUT
{
	float4 vColour	: COLOUR0;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( GS_OUTPUT Input ) : SV_TARGET
{
//	float4 vDiffuse = g_txDiffuse.Sample( g_samLinear, Input.vTexcoord );
//	return vDiffuse;
	return Input.vColour;
}