foo comps 1 min 0.0 max 1.0 speed 0.01 default 1.0
baz comps 1 min 0.0 max 1.0 speed 0.01 default 3.0
END_USER_UNIFS

#version 410

uniform mat4 mv_matrix;
uniform mat4 proj_matrix;

layout (location = 0) in vec3 vs_pos;
layout (location = 1) in vec3 vs_nor;
layout (location = 2) in vec4 vs_col;
layout (location = 3) in vec4 vs_data;

out vec3 fs_pos;
out vec3 fs_nor;
out vec4 fs_col;
out vec4 fs_data;

void main() {
  fs_pos = vs_pos;
  fs_nor = vs_nor;
  fs_col = vs_col;
  fs_data = vs_data;
  gl_Position = proj_matrix * mv_matrix * vec4(vs_pos, 1.0);
}

