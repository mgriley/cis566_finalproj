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

out vec4 fs_pos;
out vec4 fs_vel;
out vec4 fs_neighbors;
out vec4 fs_data;

out vec3 fs_nor;
out vec3 fs_col;

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

  fs_pos = vs_pos;
  fs_vel = vs_vel;
  fs_neighbors = vs_neighbors;
  fs_data = vs_data;
  // TODO - adjust this later (using the tex buffers)
  fs_nor = vec3(0.0,1.0,0.0);
  fs_col = col;
  gl_Position = proj_matrix * mv_matrix * vec4(vs_pos.xyz, 1.0);
}

