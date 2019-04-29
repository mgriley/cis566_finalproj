foo comps 1 min 0.0 max 1.0 speed 0.01 default 1.0
baz comps 1 min 0.0 max 1.0 speed 0.01 default 3.0
render_mode comps 1 min 0.0 max 3.0 speed 0.1 default 0.0
END_USER_UNIFS

#version 410

uniform mat4 mv_matrix;
uniform mat4 proj_matrix;

uniform vec4 render_mode;

layout (location = 0) in vec4 vs_pos;
layout (location = 1) in vec4 vs_vel;
layout (location = 2) in vec4 vs_neighbors;
layout (location = 3) in vec4 vs_data;

uniform samplerBuffer pos_buf;
uniform samplerBuffer vel_buf;
uniform samplerBuffer neighbors_buf;
uniform samplerBuffer data_buf;

out vec4 fs_pos;
out vec4 fs_vel;
out vec4 fs_neighbors;
out vec4 fs_data;

out vec3 fs_nor;
out vec3 fs_col;

vec3 avg_node_normal(vec3 node_pos, vec4 node_neighbors) {
  vec3 avg_nor = vec3(0.0);
  int num_nors = 0;
  for (int i = 0; i < 4; ++i) {
    int i_a = int(node_neighbors[i]);
    int i_b = int(node_neighbors[(i + 1) % 4]);
    if (i_a != -1 && i_b != -1) {
      vec3 p_a = texelFetch(pos_buf, i_a).xyz;
      vec3 p_b = texelFetch(pos_buf, i_b).xyz;
      // TODO - why is this the 'up' direction, seems like the negative
      // sign should be unneccessary
      avg_nor += normalize(-cross(p_a - node_pos, p_b - node_pos));
      num_nors += 1;
    }
  }
  return avg_nor / num_nors;
}

void main() {
  vec3 col = vec3(0.0,1.0,0.0);
  if (int(render_mode.x) == 1) {
    vec3 cold_col = vec3(0.0,0.0,1.0);
    vec3 hot_col = vec3(1.0,0.0,0.0);
    float mix_amt = clamp(vs_pos.w / 1.0f, 0.0, 1.0);
    col = mix(cold_col, hot_col, mix_amt);
  } else if (int(render_mode.x) == 2) {
    float heat_gen_amt = clamp(vs_vel.w, 0.0, 1.0);
    col = vec3(heat_gen_amt, 0.0, 0.0);
  } else if (int(render_mode.x) == 3) {
    float heat_gen_amt = vs_vel.w > 0.0 ? 1.0 : 0.0;
    col = vec3(heat_gen_amt, 0.0, 0.0);
  }

  vec3 nor = avg_node_normal(vs_pos.xyz, vs_neighbors);

  fs_pos = vs_pos;
  fs_vel = vs_vel;
  fs_neighbors = vs_neighbors;
  fs_data = vs_data;
  fs_nor = nor;
  fs_col = col;
  gl_Position = proj_matrix * mv_matrix * vec4(vs_pos.xyz, 1.0);
}

