Texture2D<float4> NoisyRadiance : register(t0);

struct PixelInput
{
    float4 position : SV_Position;
};

PixelInput vs_main(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    PixelInput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    float3 hdr = max(NoisyRadiance.Load(int3(uint2(input.position.xy), 0)).rgb, 0.0);
    float3 mapped = hdr / (1.0 + hdr);
    mapped = pow(mapped, 1.0 / 2.2);
    return float4(mapped, 1.0);
}
