#version 450

layout(push_constant) uniform PC {
    float cx;
    float cy;
    float radius;
    float r, g, b;
} pc;

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float dist = length(fragUV);   // расстояние от центра в пространстве [-1,1]

    // Отбрасываем фрагменты вне круга.
    // smoothstep даёт сглаженный край (anti-aliasing в 1 пиксель).
    float edge = 0.02 / pc.radius;          // толщина сглаживания зависит от размера
    float alpha = 1.0 - smoothstep(1.0 - edge, 1.0, dist);

    if (alpha < 0.001) discard;

    outColor = vec4(pc.r, pc.g, pc.b, alpha);
}
