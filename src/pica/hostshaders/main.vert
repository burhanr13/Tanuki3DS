#version 410 core

out vec2 texcoord;

vec2 xys[3] = vec2[](
    vec2(-1,1),
    vec2(-1,-3),
    vec2(3,1)
);

vec2 uvs[3] = vec2[](
    vec2(1,1),
    vec2(-1,1),
    vec2(1,-1)
);

void main() {
    gl_Position = vec4(xys[gl_VertexID],0,1);
    texcoord = uvs[gl_VertexID];
}
