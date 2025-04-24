#if D3D12
# define REG(reg, space) register(reg, space)
#else
# define REG(reg, space) register(reg)
#endif

Texture2D<half4> Texture : REG(t0, space2);
SamplerState Sampler : REG(s0, space2);

struct FragmentInput
{
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD0;
};

half4 FragmentMain(FragmentInput input) : SV_Target0
{
	return Texture.Sample(Sampler, input.texcoord);
}
