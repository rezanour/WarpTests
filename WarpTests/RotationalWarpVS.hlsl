cbuffer Constants
{
    float4x4 InvTWMatrix;
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
    output.Position.zw = float2(0, 1);

    float3 ray = float3(output.Position.xy, 1.f);
    ray = mul((float3x3)InvTWMatrix, ray);

    ray.xy /= ray.z;
    output.TexCoord = float2(ray.x * 0.5 + 0.5, 1 - (ray.y * 0.5 + 0.5));

    return output;
}