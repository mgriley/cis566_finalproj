const char* MORPH_BASIC_VERTEX_SRC = R"--(
#version 410

uniform int iter_num;

// use explicit locations so that these attributes in 
// different programs explicitly use the same attribute indices
layout (location = 0) in int node_type;
layout (location = 1) in vec3 pos;
// up, down, right, left
layout (location = 2) in ivec4 neighbors;
layout (location = 3) in ivec4 faces;

uniform isamplerBuffer node_type_buf;
uniform samplerBuffer pos_buf;
uniform isamplerBuffer neighbors_buf;
uniform isamplerBuffer faces_buf;

out int out_node_type;
out vec3 out_pos;
out vec3 out_vel;
out vec4 out_data;
out ivec4 out_neighbors;
out ivec4 out_faces;

vec3 face_normal(int face_index) {
  ivec4 face_vertices = texelFetch(neighbors_buf, face_index);
  // pick an arb 3 vertices on the quad
  vec3 va = texelFetch(pos_buf, face_vertices[0]).xyz;
  vec3 vb = texelFetch(pos_buf, face_vertices[1]).xyz;
  vec3 vc = texelFetch(pos_buf, face_vertices[3]).xyz;
  return normalize(cross(vb - va, vc - va));
}

void handle_vert() {
  int right_n_index = texelFetch(neighbors_buf, neighbors[3]).r;
  vec3 right_pos = texelFetch(pos_buf, right_n_index).xyz;
  vec3 mid = (right_pos + pos) * 0.5;

  vec3 new_pos;
  //new_pos = mid;
  //new_pos = pos + face_normal(faces[0]);
  //new_pos = pos + normalize(right_pos - pos) * 1.0;
  new_pos = pos + vec3(1.0,0.0,0.0);
  
  out_node_type = node_type;
  out_pos = new_pos;
  out_vel = vel;
  out_data = data;
  out_neighbors = neighbors;
  out_faces = faces;
}

void handle_face() {
  out_node_type = node_type;
  out_pos = pos;
  out_vel = vel;
  out_data = data;
  out_neighbors = neighbors;
  out_faces = faces;
}

void main() {
  if (node_type == 0) {
    handle_vert();
  } else if (node_type == 1) {
    handle_face();  
  }
}
)--";

