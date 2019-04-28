#pragma once

#include "utils.h"

struct Camera {
  mat4 cam_to_world = mat4(1.0);

  Camera();
  void set_view(vec3 eye, vec3 target);
  vec3 pos() const;
  vec3 right() const;
  vec3 up() const;
  vec3 forward() const;
};

// For use in the interleaved render buffer
struct Vertex {
  vec3 pos = vec3(0.0);
  vec3 nor = vec3(0.0);
  vec4 col = vec4(0.0);

  Vertex(vec3 pos, vec3 nor, vec4 col);
};

struct RenderState {
  GLuint vao = 0;
  GLuint prog = 0;

  int fb_width = 0;
  int fb_height = 0;

  GLuint vbo = 0;
  GLuint index_buffer = 0;
  int elem_count = 0;
  int vertex_count = 0;

  GLint unif_mv_matrix = -1;
  GLint unif_proj_matrix = -1;
  GLint unif_debug_render = -1;
  GLint unif_debug_color = -1;

  GLint pos_attrib = -1;
  GLint nor_attrib = -1;
  GLint col_attrib = -1;

  RenderState();
};

struct MorphNode {
  vec4 pos = vec4(0.0);
  vec4 vel = vec4(0.0);
  // in order of: {right, upper, left, lower} wrt surface normal
  vec4 neighbors = vec4(0.0);
  vec4 data = vec4(0.0);

  MorphNode();
  MorphNode(vec4 pos, vec4 vel, vec4 neighbors, vec4 data);
};

struct MorphNodes {
  vector<vec4> pos_vec;
  vector<vec4> vel_vec;
  vector<vec4> neighbors_vec;
  vector<vec4> data_vec;

  MorphNodes(size_t num_nodes);
  MorphNodes(vector<MorphNode> const& nodes);
  MorphNode node_at(size_t i) const;
};

string raw_node_str(MorphNode const& node);

// These serve as indices into the MorphBuffer vbos and tex_buf arrays
enum MorphBuffers {
  BUF_POS = 0,
  BUF_VEL,
  BUF_NEIGHBORS,
  BUF_DATA,

  MORPH_BUF_COUNT
};

struct MorphBuffer {
  GLuint vao = 0;
  array<GLuint, MORPH_BUF_COUNT> vbos = {0};
  array<GLuint, MORPH_BUF_COUNT> tex_bufs = {0};

  MorphBuffer();
};

struct UserUnif {
  string name;
  GLuint gl_handle = 0;
  int num_comps = 4;
  float min = 0.0;
  float max = 1.0;
  float drag_speed = 5.0;
  vec4 def_val = vec4(0.0);
  vec4 cur_val = vec4(0.0);

  UserUnif(string name, int num_comps, float min, float max,
      float drag_speed, vec4 def_val, vec4 cur_val);
};

struct MorphProgram {
  string name;
  GLuint gl_handle = 0;
  array<GLint, MORPH_BUF_COUNT> unif_samplers = {0};
  GLint unif_iter_num = -1;
  vector<UserUnif> user_unifs;

  MorphProgram(string name, vector<UserUnif>& user_unifs);
};

struct MorphState {
  // for double-buffering
  array<MorphBuffer, 2> buffers;
  // set to the index of the buffer that holds
  // the most recent simulation result
  int result_buffer_index = 0;

  string base_shader_path;
  vector<MorphProgram> programs;
  int cur_prog_index = 0;
  
  // the number of nodes used in the most recent sim
  int num_nodes = 0;

  MorphState(string base_shader_path);
};

// User controls 
struct Controls {
  // rendering
  bool render_faces;
  bool render_points;
  bool render_wireframe;

  // simulation
  bool log_input_nodes;
  bool log_output_nodes;
  bool log_render_data;
  int num_zygote_samples;
  int num_iters;

  bool cam_spherical_mode;
  Camera zoom_to_fit_cam;

  Controls();
};

struct GraphicsState {
  RenderState render_state;
  MorphState morph_state;

  GLFWwindow* window;
  Camera camera;
  Controls controls;

  GraphicsState(GLFWwindow* window, string base_shader_path);
};

