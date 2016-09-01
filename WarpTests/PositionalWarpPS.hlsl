Texture2D SourceImage;
SamplerState Sampler;

struct VertexIn
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float4 main(VertexIn input) : SV_TARGET
{
    return SourceImage.Sample(Sampler, input.TexCoord);
}
