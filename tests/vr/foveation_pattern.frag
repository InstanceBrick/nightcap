#version 450
layout(location=0) out vec4 color;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);
    color=vec4(float(p.x&1),float(p.y&1),float(((p.x/8)+(p.y/8))&1),1.0);
}
