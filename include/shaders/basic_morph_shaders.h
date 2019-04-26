const char* MORPH_BASIC_VERTEX_SRC = R"--(
#version 410

// use explicit locations so that these attributes in 
// different programs explicitly use the same attribute indices
layout (location = 0) in int node_type;
layout (location = 1) in vec3 pos;
layout (location = 2) in ivec4 neighbors;
layout (location = 3) in ivec4 faces;

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
  out_pos = pos + vec3(-1.0,0.0,0.0);
  out_neighbors = neighbors;
  out_faces = faces;
}

)--";

