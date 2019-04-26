const char* MORPH_VERTEX_SRC = R"--(
#version 410

struct MorphNode {
  int node_type;
  vec3 pos;
  ivec4 neighbors;
  ivec4 faces;
};

in int node_type;
in vec3 pos;
in ivec4 neighbors;
in ivec4 faces;

uniform isamplerBuffer node_type_buf;
uniform samplerBuffer pos_buf;
uniform isamplerBuffer neighbors_buf;
uniform isamplerBuffer faces_buf;

out int out_node_type;
out vec3 out_pos;
out ivec4 out_neighbors;
out ivec4 out_faces;

void main() {
  //ivec4 coord = texelFetch(node_type_buf, 0);
  //vec4 foo = texelFetch(pos_buf, 0);
  //ivec4 bar = texelFetch(neighbors_buf, 0);
  int baz = texelFetch(node_type_buf, 0).r;

  out_node_type = node_type;
  out_pos = pos + vec3(1.0,0.0,0.0);// + vec3(1.0,float(neighbors[1]),0.0);
  out_neighbors = neighbors;
  out_faces = faces;
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
