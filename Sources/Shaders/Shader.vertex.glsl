#version 450

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec2 i_texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(set = 1, binding = 0) uniform UBO
{
    mat4 u_viewproj;
};

void main()
{
	v_texcoord  = i_texcoord;
	gl_Position = u_viewproj * vec4(i_position, 1.0);
}
