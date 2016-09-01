cbuffer Constants
{
    float4x4 WorldViewProj;
};

struct VertexIn
{
    float3 Position : POSITION;
    float3 Color : COLOR;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR;
};

VertexOut main(VertexIn input)
{
    VertexOut output;
    output.Position = mul(WorldViewProj, float4(input.Position, 1));
    output.Color = input.Color;
	return output;
}