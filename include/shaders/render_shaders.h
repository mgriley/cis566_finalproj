const char* RENDER_VERTEX_SRC = R"--(
#version 410

uniform mat4 mv_matrix;
uniform mat4 proj_matrix;

in vec3 vs_pos;
in vec3 vs_nor;
in vec4 vs_col;

out vec4 fs_pos;
out vec3 fs_nor;
out vec4 fs_col;

void main() {
  vec4 local_pos = mv_matrix * vec4(vs_pos, 1.0);
  vec4 local_nor = mv_matrix * vec4(vs_nor, 0.0);
  
  fs_pos = local_pos;
  fs_nor = normalize(local_nor.xyz);
  fs_col = vs_col;
  gl_Position = proj_matrix * local_pos;
}

)--";

const char* RENDER_FRAGMENT_SRC = R"--(
#version 410

in vec4 fs_pos;
in vec3 fs_nor;
in vec4 fs_col;

out vec4 color;

void main() {
  vec3 light_dir = normalize(vec3(1,1,1));
  float df = clamp(dot(fs_nor.xyz, light_dir), 0.0, 1.0);
  vec3 col = df * fs_col.rgb;

  color = vec4(col, 1.0);  
}

)--";
