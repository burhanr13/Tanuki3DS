const char* mainvertsource = R"(
#version 410 core

out vec2 texcoord;

vec2 xys[4] = vec2[](
    vec2(-1,1),
    vec2(-1,-1),
    vec2(1,1),
    vec2(1,-1)
);

vec2 uvs[4] = vec2[](
    vec2(1,1),
    vec2(0,1),
    vec2(1,0),
    vec2(0,0)
);

void main() {
    gl_Position = vec4(xys[gl_VertexID],0,1);
    texcoord = uvs[gl_VertexID];
}

)";