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
  vec3 p0 = pos.xyz;
  vec3 v0 = vel.xyz;
  
  bool is_fixed = false;
  vec3 force = vec3(0.0);
  for (int i = 0; i < 4; ++i) {
    int n_i = int(neighbors[i]);
    if (n_i != -1) {
      vec3 n_pos = texelFetch(pos_buf, n_i).xyz;
      vec3 delta = n_pos - p0;
      float spring_len = length(delta);
      force += normalize(delta) * (spring_len - 0.5);
    } else {
      is_fixed = true;
    }
  }
  force += 1.0*vec3(0.0,-1.0,0.0);
  force += -0.5 * v0;

  if (is_fixed) {
    force = vec3(0.0);
  }

  float delta_t = 0.017;
  float mass = 1.0;
  vec3 accel = force / mass;
  vec3 v1 = v0 + accel*delta_t;
  vec3 p1 = p0 + v0 + accel*delta_t*delta_t/2.0;

  out_pos = vec4(p1,0.0);
  out_vel = vec4(v1,0.0);
  out_neighbors = neighbors;
  out_data = data;
}
)--";

