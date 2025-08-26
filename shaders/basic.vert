#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 color;

layout(scalar, buffer_reference) buffer VertexBuffer {
    vec3 vertices[36];
    vec3 vertexColors[36];
};

layout(scalar, push_constant) uniform T {
    VertexBuffer vertex_buffer;
    mat4 matrix;
    ivec3 chunk_position;
}
push_constants;

void main() {
    mat4 matrix = push_constants.matrix;
    vec3 vertex = push_constants.vertex_buffer.vertices[gl_VertexIndex];
    gl_Position =
        matrix * vec4(vec3(vertex + push_constants.chunk_position * 16), 1.0);
    color = push_constants.vertex_buffer.vertexColors[gl_VertexIndex];
}