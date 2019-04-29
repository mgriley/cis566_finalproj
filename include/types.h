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

struct RenderProgram {
  string name;
  GLuint gl_handle = 0;

  vector<UserUnif> user_unifs;
  GLint unif_mv_matrix = -1;
  GLint unif_proj_matrix = -1;
  GLint unif_debug_render = -1;
  GLint unif_debug_color = -1;

  RenderProgram();
  RenderProgram(string name, vector<UserUnif>& user_unifs);
};

// Note: for now, we only allow one RenderProgram (though we have
// multiple MorphProgram objects in MorphState). It would be easy
// enough allow a list but it's not needed right now.
struct RenderState {
  RenderProgram prog;

  int fb_width = 0;
  int fb_height = 0;

  GLuint index_buffer = 0;
  int elem_count = 0;
  
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

struct MorphProgram {
  string name;
  GLuint gl_handle = 0;
  array<GLint, MORPH_BUF_COUNT> unif_samplers = {0};
  GLint unif_iter_num = -1;
  GLint unif_num_nodes = -1;
  vector<UserUnif> user_unifs;

  MorphProgram(string name, vector<UserUnif>& user_unifs);
};

struct MorphState {
  // for double-buffering
  array<MorphBuffer, 2> buffers;
  // set to the index of the buffer that holds
  // the most recent simulation result
  int result_buffer_index = 0;

  vector<MorphProgram> programs;
  int cur_prog_index = 0;
  
  // the number of nodes used in the most recent sim
  int num_nodes = 0;

  MorphState();
};

// User controls 
struct Controls {
  // rendering
  bool render_faces = true;
  bool render_points = true;
  bool render_wireframe = true;

  // simulation
  bool log_input_nodes = false;
  bool log_output_nodes = false;
  bool log_render_data = false;
  int num_zygote_samples = 20;
  // for the simulation/animation pane
  int num_iters = 0;
  bool animating_sim = false;
  bool loop_at_end = false;
  int start_iter_num = 0;
  int end_iter_num = 10*1000*1000;
  int delta_iters = 0;

  bool cam_spherical_mode = true;

  Controls();
};

struct GraphicsState {
  RenderState render_state;
  MorphState morph_state;

  string base_shader_path;
  GLFWwindow* window;
  Camera camera;
  Controls controls;

  GraphicsState(GLFWwindow* window, string base_shader_path);
};

