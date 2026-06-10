#version 450

// Push constants совпадают с CirclePushConstants в Engine.hpp
layout(push_constant) uniform PC {
    float cx;
    float cy;
    float radius;
    float r, g, b;
} pc;

// Передаём UV-координаты (в пространстве круга) во фрагментный шейдер.
// (0,0) — центр, (±1,±1) — края bounding quad-а.
layout(location = 0) out vec2 fragUV;

void main()
{
    // Квадрат bounding box вокруг круга (2*radius × 2*radius)
    vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
        vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0)
    );

    vec2 uv  = corners[gl_VertexIndex];   // [-1, 1] внутри bounding quad
    vec2 pos = vec2(pc.cx, pc.cy) + uv * pc.radius;

    fragUV      = uv;
    gl_Position = vec4(pos, 0.0, 1.0);
}
