cbuffer FrameData : register(b0)
{
    float gWidth;
    float gHeight;
    float gTime;
    float gRefraction;
    float4 gBubbles[64];
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 localPosition : TEXCOORD0;
    float2 screenUv : TEXCOORD1;
    float radius : TEXCOORD2;
    float phase : TEXCOORD3;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] =
    {
        float2(-1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
        float2( 1.0,  1.0),
        float2( 1.0, -1.0)
    };

    float4 bubble = gBubbles[instanceId];
    float2 local = corners[vertexId];
    float2 centerPx = bubble.xy;
    float2 pixelPosition = centerPx + local * bubble.z;

    VSOutput output;
    output.position = float4(
        pixelPosition.x / gWidth * 2.0 - 1.0,
        1.0 - pixelPosition.y / gHeight * 2.0,
        0.0,
        1.0);
    output.localPosition = local;
    output.screenUv = pixelPosition / float2(gWidth, gHeight);
    output.radius = bubble.z;
    output.phase = bubble.w;
    return output;
}
