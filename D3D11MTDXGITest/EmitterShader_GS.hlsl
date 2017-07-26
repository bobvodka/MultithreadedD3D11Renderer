//--------------------------------------------------------------------------------------
// Globals
//--------------------------------------------------------------------------------------
cbuffer cbPerObject : register( b0 )
{
	matrix		g_mWorldViewProjection	: packoffset( c0 );
	float4		g_positions[4] : packoffset( c4 );
};

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct GS_INPUT
{
	float4 vColour	: COLOUR0;
	float2 vPosition	: POSITION;
};

struct GS_OUTPUT
{
	float4 vColour	: COLOUR0;
	float4 vPosition	: SV_POSITION;
};

//--------------------------------------------------------------------------------------
// Geometry Shader
//--------------------------------------------------------------------------------------
[maxvertexcount(4)]
void GSMain( point GS_INPUT inData[1], inout TriangleStream<GS_OUTPUT> outStream )
{
	GS_OUTPUT output;
	for(int i = 0; i < 4; i++)
	{
		float2 position = inData[0].vPosition + (g_positions[i].xy);
		output.vPosition = mul(float4(position, 6, 1), g_mWorldViewProjection);
		output.vColour = inData[0].vColour;

		outStream.Append(output);
	}

	outStream.RestartStrip();
}