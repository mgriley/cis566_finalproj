// This is a dummy fragment shader.
// Rasterization is disabled b/c we only need the transform feedback stage,
// but a shader must be provided.
const char* MORPH_DUMMY_FRAGMENT_SRC = R"--(
#version 410

in vec3 out_pos;

out vec4 color;

void main() {
  color = vec4(1.0,0.0,0.0,1.0);
}
)--";
