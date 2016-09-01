Texture2D SourceDepth;

cbuffer Constants
{
    float4x4 TWMatrix;
    float2 TextureSize;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

VertexOut main(float2 TexCoord : TEXCOORD)
{
    VertexOut output;
    output.Position.x = TexCoord.x * 2 - 1;
    output.Position.y = (1 - TexCoord.y) * 2 - 1;
    output.Position.z = SourceDepth.Load(int3(TexCoord * TextureSize, 0)).x;
    output.Position.w = 1;
    output.Position = mul(TWMatrix, output.Position);
    output.TexCoord = TexCoord;
    return output;
}
