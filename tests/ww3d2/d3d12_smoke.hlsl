// Minimal pipeline used by D3D12DeviceTests to verify shader compilation,
// input layouts, root constants and draw submission end to end.

cbuffer DrawConstants : register(b0)
{
	float4 Tint;
};

struct VSInput
{
	float3 Position : POSITION;
	float4 Color    : COLOR0;		// D3DCOLOR (BGRA8) unpacked by the input assembler
};

struct PSInput
{
	float4 Position : SV_Position;
	float4 Color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	output.Position = float4(input.Position, 1.0f);
	output.Color = input.Color * Tint;
	return output;
}

float4 PSMain(PSInput input) : SV_Target
{
	return input.Color;
}
