#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

layout(location = 0) in vec3 color;

layout(location = 0) out vec4 colorOut;

void main() { colorOut = vec4(color, 1); }