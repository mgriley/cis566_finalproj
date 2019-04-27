const char* MORPH_GROWTH_VERTEX_SRC = R"--(
#version 410

uniform int iter_num;

// use explicit locations so that these attributes in 
// different programs explicitly use the same attribute indices
layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 vel;
// right, up, left, down
layout (location = 2) in vec4 neighbors;
layout (location = 3) in vec4 data;

uniform samplerBuffer pos_buf;
uniform samplerBuffer vel_buf;
uniform samplerBuffer neighbors_buf;
uniform samplerBuffer data_buf;

out vec4 out_pos;
out vec4 out_vel;
out vec4 out_neighbors;
out vec4 out_data;

void main() {
  vec4 new_pos = pos + vec4(1.0,0.0,0.0,0.0);

  out_pos = new_pos;
  out_vel = vel;
  out_neighbors = neighbors;
  out_data = data;

}
)--";

