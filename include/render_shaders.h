const char* RENDER_VERTEX_SRC = R"--(
#version 410

uniform mat4 mv_matrix;
uniform mat4 proj_matrix;

in vec3 vs_pos;
in vec3 vs_nor;
in vec4 vs_col;

out vec3 fs_pos;
out vec3 fs_nor;
out vec4 fs_col;

void main() {
  fs_pos = vs_pos;
  fs_nor = vs_nor;
  fs_col = vs_col;
  gl_Position = proj_matrix * mv_matrix * vec4(vs_pos, 1.0);
}

)--";

const char* RENDER_FRAGMENT_SRC = R"--(
#version 410

uniform bool debug_render;
uniform vec3 debug_color;

in vec3 fs_pos;
in vec3 fs_nor;
in vec4 fs_col;

out vec4 color;

void main() {
  vec3 world_light_dir = normalize(vec3(1.0,1.0,1.0));
  vec3 world_nor = normalize(fs_nor);
  float df = clamp(dot(world_nor, world_light_dir), 0.0, 1.0);
  vec3 col = (df + 0.2) * fs_col.rgb;

  col = debug_render ? debug_color : col;
  color = vec4(col, 1.0);  
}

)--";
