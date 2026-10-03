cbuffer FrameData : register(b0)
{
    float gWidth;
    float gHeight;
    float gTime;
    float gRefraction;
    float4 gBubbles[64];
};

Texture2D gDesktop : register(t0);
SamplerState gDesktopSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 localPosition : TEXCOORD0;
    float2 screenUv : TEXCOORD1;
    float radius : TEXCOORD2;
    float phase : TEXCOORD3;
};

float3 ThinFilmColor(float2 local, float phase)
{
    float angle = atan2(local.y, local.x);
    float wave = angle * 1.35 + length(local) * 5.2 + phase;
    return 0.5 + 0.5 * cos(wave + float3(0.0, 2.1, 4.2));
}

float4 PSMain(PSInput input) : SV_Target
{
    float2 local = input.localPosition;
    float radialDistance = length(local);
    float coverage = 1.0 - smoothstep(0.985, 1.0, radialDistance);
    if (coverage <= 0.0)
    {
        discard;
    }

    float edge = smoothstep(0.78, 0.99, radialDistance) * coverage;
    float2 refractionPixels = local * input.radius * gRefraction
        * sqrt(saturate(1.0 - radialDistance * radialDistance));
    float2 sampleUv = saturate(input.screenUv + refractionPixels / float2(gWidth, gHeight));
    float3 background = gDesktop.Sample(gDesktopSampler, sampleUv).rgb;

    float3 film = ThinFilmColor(local, input.phase + gTime * 0.05);
    float filmStrength = 0.035 + 0.07 * edge;
    float3 color = background * (1.0 - filmStrength * 0.42)
        + film * filmStrength;

    float2 highlightPosition = local - float2(-0.34, -0.42);
    float highlight = exp(-dot(highlightPosition, highlightPosition) * 22.0);
    float rim = edge * (0.22 + 0.28 * saturate(0.5 + 0.5 * local.y));
    color += float3(0.78, 0.91, 1.0) * rim;
    color += float3(1.0, 0.98, 0.94) * highlight * 0.42;

    float alpha = coverage * saturate(0.96 + rim * 0.08 + highlight * 0.025);
    return float4(saturate(color) * alpha, alpha);
}
