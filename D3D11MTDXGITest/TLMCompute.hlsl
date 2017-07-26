//////////////////////////////////////////////////////////////
// Compute shader to carry out the TLM process
//
// Shiney.
//////////////////////////////////////////////////////////////

struct TLMDataBuffer
{
	float4 energyLevel;		// output energy level from this stage going to the four nodes around me
};

struct TLMInputData
{
	float	driveValue;			// value to insert into the simulation
	int	position;			// location on the grid for this data point
};

struct NodeLocations
{
	int Source;
	int2 location;
};

StructuredBuffer<TLMDataBuffer> TLMSource : register(t0);
StructuredBuffer<TLMInputData> TLMDrive : register(t1);
RWTexture2D<float4> ColourOut : register(u0);
RWStructuredBuffer<TLMDataBuffer> TLMOut : register(u1);

#define North 0
#define East 1
#define South 2
#define West 3

// WIDTH and HEIGHT defined via compiling program

float GetNodeData(int2 location, int Dest, int Source)
{
	
	if(location.y < 0)
	{
		location.y = BUFFER_HEIGHT -1;
	}
	else if(location.y >= BUFFER_HEIGHT)
	{
		location.y = 0;
	}
	
	if(location.x >= BUFFER_WIDTH)
	{
		location.x = 0;
	}
	else if( location.x < 0)
	{
		location.x = BUFFER_WIDTH - 1;
	}

	int id = location.x + (location.y * BUFFER_WIDTH);
	return TLMSource[id].energyLevel[Source];

}

#define NEW_STYLE_TLM

float4 GetNodeData(int2 location)
{
	if(location.y < 0)
	{
		location.y = BUFFER_HEIGHT - 1;
	}
	else if(location.y >= BUFFER_HEIGHT)
	{
		location.y = 0;
	}
	
	if(location.x >= BUFFER_WIDTH)
	{
		location.x = 0;
	}
	else if( location.x < 0)
	{
		location.x = BUFFER_WIDTH - 1;
	}
	
	int id = location.x + (location.y * BUFFER_WIDTH);
	return TLMSource[id].energyLevel;
	//return float4(0.0,0.0,0.0,0.0);
}

int ConvertToID(int x, int y)
{
	return x + (y * (TLM_WIDTH+2));
}

int ConvertToID(int2 position)
{
	return ConvertToID(position.x, position.y);
}

groupshared TLMInputData driveData[TLM_DATAPOINTS];
groupshared float4 sourceData[(TLM_WIDTH+2) * (TLM_HEIGHT+2)];

[numthreads(TLM_WIDTH,TLM_HEIGHT,1)]
void TLMMain(uint3 GroupID : SV_GroupID, uint3 DispatchThreadID : SV_DispatchThreadID, uint3 GroupThreadID : SV_GroupThreadID, uint GroupIndex : SV_GroupIndex)
{
	if(GroupIndex < TLM_DATAPOINTS)
	{
		driveData[GroupIndex] = TLMDrive[GroupIndex];
	}

	int2 virtualPosition = int2(GroupThreadID.x + 1,GroupThreadID.y + 1);
	// read in source data
	if(GroupThreadID.x == 0) // we are on the left
	{
		sourceData[ConvertToID(virtualPosition.x - 1,virtualPosition.y)] = GetNodeData(int2(DispatchThreadID.x - 1, DispatchThreadID.y ));
	}
	else if(GroupThreadID.x == TLM_WIDTH - 1) // we are on the right
	{
		sourceData[ConvertToID(virtualPosition.x + 1,virtualPosition.y)] = GetNodeData(int2(DispatchThreadID.x + 1, DispatchThreadID.y ));
	}

	if(GroupThreadID.y == 0) // top row
	{
		sourceData[ConvertToID(virtualPosition.x , virtualPosition.y - 1)] = GetNodeData(int2(DispatchThreadID.x, DispatchThreadID.y - 1 ));
	}
	else if(GroupThreadID.y == (TLM_HEIGHT-1) )	// bottom row
	{
		sourceData[ConvertToID(virtualPosition.x, virtualPosition.y + 1)] = GetNodeData(int2(DispatchThreadID.x, DispatchThreadID.y + 1));
	}

	// Finally sample ourself
	sourceData[ConvertToID(virtualPosition)] = GetNodeData(int2(DispatchThreadID.x, DispatchThreadID.y ));
	
	GroupMemoryBarrierWithGroupSync();	// wait until the above is done
	
#ifdef NEW_STYLE_TLM
	// Grab the energy we need for our node	
	float4 energyLevel = float4(0,0,0,0);
	
	energyLevel[North] 	= sourceData[ConvertToID(virtualPosition.x, 	virtualPosition.y - 1)][South];
	energyLevel[East] 	= sourceData[ConvertToID(virtualPosition.x - 1, virtualPosition.y	 )][West];
	energyLevel[South] 	= sourceData[ConvertToID(virtualPosition.x, 	virtualPosition.y + 1)][North];
	energyLevel[West]	= sourceData[ConvertToID(virtualPosition.x + 1, virtualPosition.y	 )][East];

#else
	NodeLocations location[4];
	location[North].location = int2(DispatchThreadID.x,     (DispatchThreadID.y - 1) ); location[North].Source = South;
	location[East].location	 = int2(DispatchThreadID.x - 1, DispatchThreadID.y );       location[East].Source = West;
	location[South].location = int2(DispatchThreadID.x,     (DispatchThreadID.y + 1) ); location[South].Source = North;
	location[West].location  = int2(DispatchThreadID.x + 1, DispatchThreadID.y );       location[West].Source = East;

	float4 energyLevel;

	for(int i = 0; i < 4; i++)
	{
		energyLevel[i] = GetNodeData(location[i].location, i,location[i].Source);
	}

#endif	
	// See if we have any drive values to bring in

//	ColourOut[int2(DispatchThreadID.x,DispatchThreadID.y)] = energyLevel;

	int id = DispatchThreadID.x + (DispatchThreadID.y * BUFFER_WIDTH);
	
	for(int j = 0; j < TLM_DATAPOINTS; j++)
	{
		if(driveData[j].position == id)
		{
			energyLevel += float4(driveData[j].driveValue,driveData[j].driveValue,driveData[j].driveValue,driveData[j].driveValue);
		}
	}
	
	// Transfer energy about
	float4 transfer;
	transfer[North] = dot(energyLevel, float4(-0.5f, 0.5f, 0.5f, 0.5f)); // introduce some decay on the enery being sent around
	transfer[East]  = dot(energyLevel, float4( 0.5f,-0.5f, 0.5f, 0.5f));
	transfer[South] = dot(energyLevel, float4( 0.5f, 0.5f,-0.5f, 0.5f));
	transfer[West]  = dot(energyLevel, float4( 0.5f, 0.5f, 0.5f,-0.5f));
	transfer *= float4(0.995f, 0.995f,0.995f,0.995f);
	
	// Write out the colour values we are scattering
	TLMOut[id].energyLevel = transfer;
	
	// write out our final colour
 	ColourOut[int2(DispatchThreadID.x,DispatchThreadID.y)] = float4(transfer[North], transfer[South],transfer[East],transfer[West]);
 	//ColourOut[int2(DispatchThreadID.x,DispatchThreadID.y)] = float4(0.0,0.0,0.0,0.0); 
	
}

