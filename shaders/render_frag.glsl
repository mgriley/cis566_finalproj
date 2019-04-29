END_USER_UNIFS

#version 410

uniform bool debug_render;
uniform vec3 debug_color;

in vec4 fs_pos;
in vec4 fs_vel;
in vec4 fs_data;
in vec3 fs_nor;
in vec3 fs_col;

out vec4 color;

void main() {
  vec3 world_light_dir = normalize(vec3(1.0,1.0,1.0));
  vec3 world_nor = normalize(fs_nor);
  float df = clamp(dot(world_nor, world_light_dir), 0.0, 1.0);
  vec3 col = (df + 0.2) * fs_col;

  col = debug_render ? fs_col : col;
  color = vec4(col, 1.0);  
}

