#if D3D12
# define REG(reg, space) register(reg, space)
#else
# define REG(reg, space) register(reg)
#endif

struct VertexInput
{
	float3 position : TEXCOORD0;
	float2 texcoord : TEXCOORD1;
};

cbuffer VertexUniform : REG(b0, space1)
{
	float4x4 viewproj : packoffset(c0);
};

struct VertexOutput
{
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD0;
};

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(viewproj, float4(input.position, 1.0));
	output.texcoord = input.texcoord;
	return output;
}
