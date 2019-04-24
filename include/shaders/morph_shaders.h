const char* MORPH_VERTEX_SRC = R"--(
#version 410

in vec3 pos;
//uniform samplerBuffer pos_buffer;

out vec3 out_pos;

void main() {
  out_pos = pos;
}

)--";

// This is a dummy fragment shader.
// Rasterization is disabled b/c we only need the transform feedback stage.
const char* MORPH_FRAGMENT_SRC = R"--(
#version 410

in vec3 out_pos;

out vec4 color;

void main() {
  color = vec4(1.0,0.0,0.0,1.0);
}
)--";
