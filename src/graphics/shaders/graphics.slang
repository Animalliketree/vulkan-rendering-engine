struct VSInput {
    float3 pos;
    float3 normal;
    float2 uv;
};

struct ShaderData {
    float4x4 proj;
    float4x4 view;
    float4x4 model;
};
ConstantBuffer<ShaderData> data;

struct VSOutput {
    float4 pos : SV_Position;
    float3 normal;
    float2 uv;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(data.proj, mul(data.view, mul(data.model, float4(input.pos.xyz, 1.0))));
    output.normal = mul((float3x3)mul(data.view, data.model), input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_Target {
    return float4(inVert.normal, 1.0);
}
