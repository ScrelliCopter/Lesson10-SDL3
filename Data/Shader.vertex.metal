#include <metal_stdlib>
#include <simd/simd.h>

struct VertexInput
{
	float3 position [[attribute(0)]];
	float2 texcoord [[attribute(1)]];
};

struct VertexUniform
{
	metal::float4x4 viewproj;
};

struct VertexOutput
{
	float4 position [[position]];
	float2 texcoord;
};

vertex VertexOutput VertexMain(
	VertexInput in [[stage_in]],
	constant VertexUniform& u [[buffer(0)]])
{
	VertexOutput out;
	out.position = u.viewproj * float4(in.position, 1.0);
	out.texcoord = in.texcoord;
	return out;
}
