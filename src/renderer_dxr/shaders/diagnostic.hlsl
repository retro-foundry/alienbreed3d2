struct DiagnosticVertex {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

DiagnosticVertex vs_main(uint vertex_id : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(0.0, -0.72),
        float2(-0.72, 0.62),
        float2(0.72, 0.62)
    };
    static const float3 colors[3] = {
        float3(0.10, 0.78, 1.00),
        float3(0.86, 0.16, 0.55),
        float3(0.98, 0.72, 0.12)
    };
    DiagnosticVertex output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = colors[vertex_id];
    return output;
}

float4 ps_main(DiagnosticVertex input) : SV_Target0
{
    return float4(input.color, 1.0);
}
