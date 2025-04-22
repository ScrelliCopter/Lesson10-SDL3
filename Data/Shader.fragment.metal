#include <metal_stdlib>
#include <simd/simd.h>

struct FragmentInput
{
	float4 position [[position]];
	float2 texcoord;
};

fragment half4 FragmentMain(
	FragmentInput in [[stage_in]],
	metal::texture2d<half, metal::access::sample> texture [[texture(0)]],
	metal::sampler sampler [[sampler0]])
{
	return texture.sample(sampler, in.texcoord);
}
