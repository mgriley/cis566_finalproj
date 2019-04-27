const char* MORPH_GROWTH_VERTEX_SRC = R"--(
#version 410

uniform int iter_num;

// use explicit locations so that these attributes in 
// different programs explicitly use the same attribute indices
layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 vel;
// up, down, right, left
layout (location = 2) in vec4 neighbors;
layout (location = 3) in vec4 faces;

uniform samplerBuffer pos_buf;
uniform samplerBuffer vel_buf;
uniform samplerBuffer neighbors_buf;
uniform samplerBuffer faces_buf;

out vec4 out_pos;
out vec4 out_vel;
out vec4 out_neighbors;
out vec4 out_faces;

void handle_vert() {
  new_pos = pos + vec3(1.0,0.0,0.0);
  
  out_pos = new_pos;
  out_vel = vel;
  out_neighbors = neighbors;
  out_faces = faces;
}

void handle_face() {
  out_pos = pos;
  out_vel = vel;
  out_neighbors = neighbors;
  out_faces = faces;
}

void main() {
  if (pos.z == 0.0) {
    handle_vert();
  } else if (pos.z == 1.0) {
    handle_face();  
  }
}
)--";

