#include "app.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <execinfo.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <chrono>
#include <thread>
#include <array>
#include <vector>
#include <utility>
#include <tuple>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <cstddef>
#include "shaders/render_shaders.h"
#include "shaders/dummy_morph_shaders.h"
#include "shaders/basic_morph_shaders.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

using namespace std;
using namespace glm;

string vec3_str(vec3 v) {
  array<char, 100> s;
  sprintf(s.data(), "[%5.2f, %5.2f, %5.2f]", v[0], v[1], v[2]);
  return string(s.data());
}

string ivec4_str(ivec4 v) {
  array<char, 100> s;
  sprintf(s.data(), "[%4d, %4d, %4d, %4d]", v[0], v[1], v[2], v[3]); 
  return string(s.data());
}

struct Camera {
  mat4 cam_to_world;
  Camera();
};

Camera::Camera() {
  vec3 forward(0.0,0.0,-1.0);
  vec3 up(0.0,1.0,0.0);
  vec3 right = cross(up, forward);
  vec3 pos(0.0,0.0,10.0);

  cam_to_world[0] = vec4(right, 0.0);
  cam_to_world[1] = vec4(up, 0.0);
  cam_to_world[2] = vec4(forward, 0.0);
  cam_to_world[3] = vec4(pos, 1.0);
}

// For use in the interleaved render buffer
struct Vertex {
  vec3 pos;
  vec3 nor;
  vec4 col;
};

struct RenderState {
  GLuint vao;
  GLuint prog;

  int fb_width, fb_height;

  GLuint vbo;
  GLuint index_buffer;
  int elem_count;
  int vertex_count;

  GLint unif_mv_matrix;
  GLint unif_proj_matrix;
  GLint unif_debug_render;
  GLint unif_debug_color;

  GLint pos_attrib;
  GLint nor_attrib;
  GLint col_attrib;
};

// For use in the interleaved transform feedback buffer
enum MorphNodeType {
  TYPE_VERTEX = 0,
  TYPE_FACE
};

struct MorphNode {
  int node_type;
  vec3 pos;
  // in order of: upper, lower, right, left
  ivec4 neighbors;
  ivec4 faces;

  MorphNode();
  MorphNode(int node_type, vec3 pos,
      ivec4 neighbors, ivec4 faces);
};

MorphNode::MorphNode():
  node_type(TYPE_VERTEX),
  pos(0.0),
  neighbors(-1),
  faces(-1)
{
}

MorphNode::MorphNode(int node_type, vec3 pos,
    ivec4 neighbors, ivec4 faces) :
  node_type(node_type),
  pos(pos),
  neighbors(neighbors),
  faces(faces)
{
}

struct MorphNodes {
  vector<int> node_type_vec;
  vector<vec3> pos_vec;
  vector<ivec4> neighbors_vec;
  vector<ivec4> faces_vec;

  MorphNodes(size_t num_nodes);
  MorphNodes(vector<MorphNode> const& nodes);
  MorphNode node_at(size_t i) const;
};

MorphNodes::MorphNodes(size_t num_nodes) :
  node_type_vec(num_nodes),
  pos_vec(num_nodes),
  neighbors_vec(num_nodes),
  faces_vec(num_nodes)
{
}

MorphNodes::MorphNodes(vector<MorphNode> const& nodes) :
  node_type_vec(nodes.size()),
  pos_vec(nodes.size()),
  neighbors_vec(nodes.size()),
  faces_vec(nodes.size())
{
  for (int i = 0; i < nodes.size(); ++i) {
    MorphNode const& node = nodes[i];
    node_type_vec[i] = node.node_type;
    pos_vec[i] = node.pos;
    neighbors_vec[i] = node.neighbors;
    faces_vec[i] = node.faces;
  }
}

MorphNode MorphNodes::node_at(size_t i) const {
  return MorphNode(node_type_vec[i],
      pos_vec[i], neighbors_vec[i], faces_vec[i]);
}

string raw_node_str(MorphNode const& node) {
  array<char, 200> s;
  sprintf(s.data(), "type: %d pos: %s, neighbors: %s, faces: %s",
        node.node_type,
        vec3_str(node.pos).c_str(), ivec4_str(node.neighbors).c_str(),
        ivec4_str(node.faces).c_str());
  return string(s.data());
}

// a more compact node string
string node_str(MorphNode const& node) {
  array<char, 200> s;
  if (node.node_type == TYPE_VERTEX) {
    sprintf(s.data(), "VERT pos: %s, neighbors: %s, faces: %s",
        vec3_str(node.pos).c_str(), ivec4_str(node.neighbors).c_str(),
        ivec4_str(node.faces).c_str());
  } else {
    sprintf(s.data(), "FACE verts: %s", ivec4_str(node.neighbors).c_str());
  }
  return string(s.data());
}

// These serve as indices into the MorphBuffer vbos and tex_buf arrays
enum MorphBuffers {
  BUF_NODE_TYPE = 0,
  BUF_POS,
  BUF_NEIGHBORS,
  BUF_FACES,

  MORPH_BUF_COUNT
};

struct MorphBuffer {
  GLuint vao;
  array<GLuint, MORPH_BUF_COUNT> vbos;
  array<GLuint, MORPH_BUF_COUNT> tex_bufs;
};

struct MorphProgram {
  string name;
  GLuint gl_handle;
  array<GLint, MORPH_BUF_COUNT> unif_samplers;
  GLint unif_iter_num;

  MorphProgram(string name);
};

MorphProgram::MorphProgram(string name) :
  name(name),
  gl_handle(-1),
  unif_iter_num(-1)
{
}

struct MorphState {
  // for double-buffering
  array<MorphBuffer, 2> buffers;
  // set to the index of the buffer that holds
  // the most recent simulation result
  int result_buffer_index;

  vector<MorphProgram> programs;
  int cur_prog_index;
  
  // the number of nodes used in the most recent sim
  int num_nodes;

  MorphState();
};

MorphState::MorphState() :
  result_buffer_index(0),
  cur_prog_index(0),
  num_nodes(0)
{
}

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

  Controls();
};

Controls::Controls() :
  render_faces(true),
  render_points(true),
  render_wireframe(true),
  log_input_nodes(false),
  log_output_nodes(false),
  log_render_data(false),
  num_zygote_samples(4),
  num_iters(1)
{
}

struct GraphicsState {
  Camera camera;
  RenderState render_state;
  MorphState morph_state;

  // UI controls:
  Controls controls;
};

void handle_segfault(int sig_num) {
  array<void*, 15> frames{};
  int num_frames = backtrace(frames.data(), frames.size());
  fprintf(stdout, "SEGFAULT to stdout");
  fprintf(stderr, "SEGFAULT signal: %d\n", sig_num);
  backtrace_symbols_fd(frames.data(), num_frames, STDERR_FILENO);
  exit(1);
}

void glfw_error_callback(int error_code, const char* error_msg) {
  printf("GLFW error: %d, %s", error_code, error_msg);
}

void log_opengl_info() {
  // main info
  const GLubyte* vendor = glGetString(GL_VENDOR);
  const GLubyte* renderer = glGetString(GL_RENDERER);
  const GLubyte* version = glGetString(GL_VERSION);
  const GLubyte* glsl_version = glGetString(GL_SHADING_LANGUAGE_VERSION);
  printf("vendor: %s\nrenderer: %s\nversion: %s\nglsl version: %s\n",
      vendor, renderer, version, glsl_version);  

  // secondary info
  GLint value = 0;
  glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &value);
  printf("GL_MAX_TEXTURE_BUFFER_SIZE %d\n", value);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
  printf("GL_MAX_TEXTURE_SIZE %d\n", value);
}

void log_gl_errors(string msg) {
  GLenum error_code = glGetError();
  if (error_code == GL_NO_ERROR) {
    return;
  }
  string error_name;
  switch (error_code) {
  case GL_INVALID_ENUM:
    error_name = "INVALID_ENUM";
    break;
  case GL_INVALID_VALUE:
    error_name = "INVALID_VALUE";
    break;
  case GL_INVALID_OPERATION:
    error_name = "INVALID_OPERATION";
    break;
  case GL_INVALID_FRAMEBUFFER_OPERATION:
    error_name = "GL_INVALID_FRAMEBUFFER_OPERATION";
    break;
  case GL_OUT_OF_MEMORY:
    error_name = "GL_OUT_OF_MEMORY";
    break;
  default:
    error_name = "unknown error code";
    break;
  };
  printf("%s: %s\n", msg.c_str(), error_name.c_str());
}

void log_program_info_logs(string msg, GLuint program) {
  GLint log_len = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
  if (log_len > 0) {
    vector<GLchar> buffer(log_len);
    GLsizei len_written = 0;
    glGetProgramInfoLog(program, buffer.size(), &len_written, buffer.data());
    printf("%s:\n%s\n", msg.data(), buffer.data());
  }
}

void log_shader_info_logs(string msg, GLuint shader) {
  GLint log_len = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
  if (log_len > 0) {
    vector<GLchar> buffer(log_len);
    GLsizei len_written = 0;
    glGetShaderInfoLog(shader, buffer.size(), &len_written, buffer.data());
    printf("%s:\n%s\n", msg.data(), buffer.data());
  }
}

GLuint setup_program(string prog_name,
    const GLchar* vertex_src, const GLchar* frag_src, bool is_morph_prog) {
  GLuint program = glCreateProgram();

  GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &vertex_src, nullptr);
  glCompileShader(vertex_shader);
  log_shader_info_logs(prog_name + ", vertex shader", vertex_shader);
  glAttachShader(program, vertex_shader);

  GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &frag_src, nullptr);
  glCompileShader(fragment_shader);
  log_shader_info_logs(prog_name + ", fragment shader", fragment_shader);
  glAttachShader(program, fragment_shader);

  if (is_morph_prog) {
    vector<const char*> varyings = {
      "out_node_type",
      "out_pos",
      "out_neighbors",
      "out_faces"
    };
    glTransformFeedbackVaryings(program, varyings.size(),
        varyings.data(), GL_SEPARATE_ATTRIBS);
  }

  glLinkProgram(program);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  log_gl_errors("setup program");

  return program;
}

// Size in bytes of the respective buffers
// TODO - check these later
const GLsizeiptr RENDER_VBO_SIZE = (GLsizeiptr) 1e8;
const GLsizeiptr RENDER_INDEX_BUFFER_SIZE = (GLsizeiptr) 1e8;

void init_render_state(GraphicsState& g_state) {
  RenderState& r_state = g_state.render_state;

  glEnable(GL_DEPTH_TEST);

  glGenVertexArrays(1, &r_state.vao);
  glBindVertexArray(r_state.vao);

  glGenBuffers(1, &r_state.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, r_state.vbo);
  // TODO - check this later (may not want static draw)
  glBufferData(GL_ARRAY_BUFFER, RENDER_VBO_SIZE, nullptr, GL_STATIC_DRAW);
  
  glGenBuffers(1, &r_state.index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
  // TODO - check this later (may not want static draw)
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, RENDER_INDEX_BUFFER_SIZE, nullptr, GL_STATIC_DRAW);

  r_state.prog = setup_program(
      "render program", RENDER_VERTEX_SRC, RENDER_FRAGMENT_SRC, false);
  r_state.unif_mv_matrix = glGetUniformLocation(
      r_state.prog, "mv_matrix");
  r_state.unif_proj_matrix = glGetUniformLocation(
      r_state.prog, "proj_matrix");
  r_state.unif_debug_render = glGetUniformLocation(
      r_state.prog, "debug_render");
  r_state.unif_debug_color = glGetUniformLocation(
      r_state.prog, "debug_color");
  assert(r_state.unif_mv_matrix != -1);
  assert(r_state.unif_proj_matrix != -1);
  assert(r_state.unif_debug_render != -1);
  assert(r_state.unif_debug_color != -1);

  // interleave the attributes within the single VBO

  r_state.pos_attrib = glGetAttribLocation(r_state.prog, "vs_pos");
  r_state.nor_attrib = glGetAttribLocation(r_state.prog, "vs_nor");
  r_state.col_attrib = glGetAttribLocation(r_state.prog, "vs_col");
  assert(r_state.pos_attrib != -1);
  assert(r_state.nor_attrib != -1);
  assert(r_state.col_attrib != -1);

  glVertexAttribPointer(r_state.pos_attrib, 3, GL_FLOAT, GL_FALSE,
      sizeof(Vertex), (void*) offsetof(Vertex, pos));
  glVertexAttribPointer(r_state.nor_attrib, 3, GL_FLOAT, GL_FALSE,
      sizeof(Vertex), (void*) offsetof(Vertex, nor));
  glVertexAttribPointer(r_state.col_attrib, 4, GL_FLOAT, GL_FALSE,
      sizeof(Vertex), (void*) offsetof(Vertex, col));

  glEnableVertexAttribArray(r_state.pos_attrib);
  glEnableVertexAttribArray(r_state.nor_attrib);
  glEnableVertexAttribArray(r_state.col_attrib);
}

// The maximum # of morph nodes
const int MAX_NUM_MORPH_NODES = (int) (1e8 / (float) sizeof(MorphNode));

MorphProgram init_morph_program(const char* prog_name,
    const char* vertex_src) {
  MorphProgram prog(prog_name);
  prog.gl_handle = setup_program(
      prog_name, vertex_src, MORPH_DUMMY_FRAGMENT_SRC, true);
  
  // Note: we do not need to get the input attribute locations
  // b/c they are explicit in each program

  // setup uniforms
  glUseProgram(prog.gl_handle);
  prog.unif_iter_num = glGetUniformLocation(
      prog.gl_handle, "iter_num");
  if (prog.unif_iter_num == -1) {
    printf("%s: WARNING iter_num is -1\n", prog_name);
  }

  // setup texture buffer samplers
  vector<const char*> unif_sampler_names = {
    "node_type_buf", "pos_buf", "neighbors_buf", "faces_buf"
  };
  for (int i = 0; i < prog.unif_samplers.size(); ++i) {
    prog.unif_samplers[i] = glGetUniformLocation(
        prog.gl_handle, unif_sampler_names[i]);
    if (prog.unif_samplers[i] != -1) {
      // set the sampler i to use texture unit i. When rendering,
      // texture buffer i will be bound to texture unit i, and 
      // texture buffer i is backed by VBO i (containing the data
      // for the ith member of the MorphNode struct)
      glUniform1i(prog.unif_samplers[i], i);
    } else {
      printf("%s: WARNING sampler is -1: %s\n", prog_name,
          unif_sampler_names[i]);
    }
  }  
  return prog;
}

void init_morph_state(GraphicsState& g_state) {
  MorphState& m_state = g_state.morph_state;

  // setup all of the morph programs
  // each pair is {program name, program src}
  vector<pair<const char*, const char*>> programs = {
    {"basic", MORPH_BASIC_VERTEX_SRC},
    {"dummy", MORPH_DUMMY_VERTEX_SRC}
  };
  for (auto& elem : programs) {
    MorphProgram prog = init_morph_program(elem.first, elem.second);
    m_state.programs.push_back(prog);
  }

  // Setup the VAO and other state for each of the two buffers
  // used for double-buffering
  for (MorphBuffer& m_buf : m_state.buffers) {
    glGenVertexArrays(1, &m_buf.vao);
    glBindVertexArray(m_buf.vao);

    // setup VBOs
    // each tuple is {sizeof element, components per element,
    // internal format of component, is integer format}
    vector<tuple<int, int, GLenum, bool>> vbo_params = {
      {sizeof(MorphNode().node_type), 1, GL_INT, true},
      {sizeof(MorphNode().pos), 3, GL_FLOAT, false},
      {sizeof(MorphNode().neighbors), 4, GL_INT, true},
      {sizeof(MorphNode().faces), 4, GL_INT, true}
    };
    glGenBuffers(m_buf.vbos.size(), m_buf.vbos.data());
    for (int i = 0; i < m_buf.vbos.size(); ++i) {
      auto& params = vbo_params[i];
      glBindBuffer(GL_ARRAY_BUFFER, m_buf.vbos[i]);
      glBufferData(GL_ARRAY_BUFFER,
          MAX_NUM_MORPH_NODES * get<0>(params), nullptr, GL_DYNAMIC_COPY);

      // Note that we only need to setup these attributes once for each VAO
      // b/c their locations are explicitly the same in each program
      if (get<3>(params)) {
        glVertexAttribIPointer(i,
            get<1>(params), get<2>(params), 0, nullptr);
      } else {
        glVertexAttribPointer(i,
            get<1>(params), get<2>(params), GL_FALSE, 0, nullptr);
      }
      glEnableVertexAttribArray(i);
    }

    // setup texture buffers
    glGenTextures(m_buf.tex_bufs.size(), m_buf.tex_bufs.data());
    vector<GLenum> internal_formats = {
      GL_R32I, GL_RGB32F, GL_RGBA32I, GL_RGBA32I
    };
    for (int i = 0; i < m_buf.tex_bufs.size(); ++i) {
      glBindTexture(GL_TEXTURE_BUFFER, m_buf.tex_bufs[i]);
      glTexBuffer(GL_TEXTURE_BUFFER, internal_formats[i], m_buf.vbos[i]);
    }
  }
}

void setup_opengl(GraphicsState& state) {
  log_opengl_info();
  
  init_render_state(state);
  init_morph_state(state);

  log_gl_errors("done setup_opengl");
}

void write_nodes_to_vbos(MorphBuffer& m_buf, MorphNodes& node_vecs) {

  // each pair is {element size, data pointer}
  vector<pair<int, GLvoid*>> data_params = {
    {sizeof(int), node_vecs.node_type_vec.data()},
    {sizeof(vec3), node_vecs.pos_vec.data()},
    {sizeof(ivec4), node_vecs.neighbors_vec.data()},
    {sizeof(ivec4), node_vecs.faces_vec.data()}
  };
  int num_nodes = node_vecs.node_type_vec.size();
  glBindVertexArray(m_buf.vao);
  for (int i = 0; i < m_buf.vbos.size(); ++i) {
    glBindBuffer(GL_ARRAY_BUFFER, m_buf.vbos[i]);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        data_params[i].first * num_nodes, data_params[i].second);
  }
}

MorphNodes read_nodes_from_vbos(MorphState& m_state) {
  MorphBuffer& target_buf = m_state.buffers[m_state.result_buffer_index];
  MorphNodes node_vecs(m_state.num_nodes);

  // each pair is {element size, target array}
  vector<pair<int, GLvoid*>> data_params = {
    {sizeof(int), node_vecs.node_type_vec.data()},
    {sizeof(vec3), node_vecs.pos_vec.data()},
    {sizeof(ivec4), node_vecs.neighbors_vec.data()},
    {sizeof(ivec4), node_vecs.faces_vec.data()}
  };
  glBindVertexArray(target_buf.vao);
  for (int i = 0; i < target_buf.vbos.size(); ++i) {
    glBindBuffer(GL_ARRAY_BUFFER, target_buf.vbos[i]);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0,
      m_state.num_nodes * data_params[i].first, data_params[i].second);
  }
  return node_vecs;
}

// helper for generate_render_data
// The nodes must be given in CCW winding order
void add_triangle_face(vector<Vertex>& vertices, vector<GLuint>& indices,
    MorphNode& node_a, MorphNode& node_b, MorphNode& node_c) {
  
  // calc the normal from the face positions.
  vec3 nor = cross(node_b.pos - node_a.pos, node_c.pos - node_a.pos);
  if (dot(nor, nor) < 1e-12) {
    // the face has no area, so skip it
    return;
  }
  nor = normalize(nor);
  GLuint index_offset = vertices.size();
  vector<MorphNode> nodes = {node_a, node_b, node_c};
  for (auto& node : nodes) {
    Vertex vertex;
    vertex.pos = node.pos;
    vertex.nor = nor;
    vertex.col = vec4(0.0,1.0,0.0,1.0);
    vertices.push_back(vertex);
  }
  // add the new indices
  vector<GLuint> new_indices = {
    index_offset, index_offset + 1, index_offset + 2
  };
  indices.insert(indices.end(),
      new_indices.begin(), new_indices.end());
}

// Use the simulation data to generate render data
void generate_render_data(GraphicsState& g_state) {  
  MorphState& m_state = g_state.morph_state;

  MorphNodes node_vecs = read_nodes_from_vbos(m_state);
    
  if (g_state.controls.log_output_nodes) {
    // log nodes
    printf("output nodes (%d):\n", m_state.num_nodes); 
    for (int i = 0; i < node_vecs.node_type_vec.size(); ++i) {
      MorphNode node = node_vecs.node_at(i);
      printf("%4d %s\n", i, raw_node_str(node).c_str());
    }
    printf("\n\n");
  }

  // convert the Node data to render data
  
  vector<Vertex> vertices;
  vector<GLuint> indices;

  for (int i = 0; i < node_vecs.node_type_vec.size(); ++i) {
    MorphNode node = node_vecs.node_at(i);
    if (node.node_type == TYPE_FACE) {
      vector<MorphNode> face_nodes;
      for (int j = 0; j < 4; ++j) {
        int node_index = node.neighbors[j];
        face_nodes.push_back(node_vecs.node_at(node_index));
      }
      // generate 2 triangle faces for each quad
      add_triangle_face(vertices, indices,
          face_nodes[0], face_nodes[1], face_nodes[2]);
      add_triangle_face(vertices, indices,
          face_nodes[0], face_nodes[2], face_nodes[3]);
    }
  }

  if (g_state.controls.log_render_data) {
    // log data
    printf("vertex data (%lu):\n", vertices.size());
    for (int i = 0; i < vertices.size(); ++i) {
      Vertex& v = vertices[i];
      printf("%4d %s\n", i, vec3_str(v.pos).c_str());
    }
    printf("\n\nindex data (%lu):\n", indices.size());
    for (int i = 0; i < indices.size(); i += 3) {
      ivec3 face(indices[i], indices[i + 1], indices[i + 2]);
      printf("%4d %s\n", i, to_string(face).c_str());
    }
  }

  // write the render data to render buffers
  RenderState& r_state = g_state.render_state;
  size_t vert_data_len = vertices.size() * sizeof(Vertex);
  size_t index_data_len = indices.size() * sizeof(GLuint);
  assert(vert_data_len <= RENDER_VBO_SIZE);
  assert(index_data_len <= RENDER_INDEX_BUFFER_SIZE);
  glBindBuffer(GL_ARRAY_BUFFER, r_state.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vert_data_len, vertices.data());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_data_len, indices.data());
  r_state.vertex_count = vertices.size();
  r_state.elem_count = indices.size();
}

vec3 gen_sphere(vec2 unit) {
  float v_angle = unit[1] * M_PI;
  float h_angle = unit[0] * 2.0 * M_PI;
  return vec3(
      sin(v_angle) * cos(h_angle),
      sin(v_angle) * sin(h_angle),
      cos(v_angle));
}

vec3 gen_square(vec2 unit) {
  return vec3(unit, 0);
}

vec3 gen_plane(vec2 unit) {
  vec2 plane_pos = 1.0f * (2.0f * (unit - 0.5f));
  return vec3(plane_pos, 0.0f);
}

// Modulo but wraps -ve values to the +ve range, giving a positive result
// Copied from: https://stackoverflow.com/questions/14997165/fastest-way-to-get-a-positive-modulo-in-c-c
inline int pos_mod(int i, int n) {
  return (i % n + n) % n;
}

// coord to vertex index
int coord_to_index_old(ivec2 coord, ivec2 samples) {
  return pos_mod(coord[0], samples[0]) + samples[0] * pos_mod(coord[1], samples[1]);
}

// Helper for gen_morph_data
// returns -1 if the coord is outside the plane
int coord_to_index(ivec2 coord, ivec2 samples) {
  if ((0 <= coord[0] && coord[0] < samples[0]) &&
      (0 <= coord[1] && coord[1] < samples[1])) {
    return coord[0] % samples[0] + samples[0] * (coord[1] % samples[1]);
  } else {
    return -1;
  }
}

// Helper for gen_morph_data
// coord is the coord of the bot-left vertex of the face
// returns -1 if no such face
int face_index(ivec2 coord, ivec2 samples, int index_offset) {
  if ((0 <= coord[0] && coord[0] < samples[0] - 1) &&
      (0 <= coord[1] && coord[1] < samples[1] - 1)) {
    int base_index = coord[0] % samples[0] + samples[0] * (coord[1] % samples[1]);
    return base_index + index_offset;
  } else {
    return -1;
  }
}

vector<MorphNode> gen_morph_data(ivec2 samples) {
  // Note that the full node array will be [vertex_nodes..., face_nodes...],
  // so we offset the face indices with the num of vertices
  int num_vertices = samples[0] * samples[1];
  vector<MorphNode> vertex_nodes;
  vector<MorphNode> face_nodes;
  for (int y = 0; y < samples[1]; ++y) {
    for (int x = 0; x < samples[0]; ++x) {
      ivec2 coord(x, y);
      vec3 pos = gen_plane(vec2(coord) / vec2(samples - 1));

      int upper_neighbor = coord_to_index(coord + ivec2(0, 1), samples);
      int lower_neighbor = coord_to_index(coord + ivec2(0, -1), samples);
      int right_neighbor = coord_to_index(coord + ivec2(1, 0), samples);
      int left_neighbor = coord_to_index(coord + ivec2(-1, 0), samples);
      ivec4 neighbors(upper_neighbor, lower_neighbor, right_neighbor, left_neighbor);

      // the index of the face is the index of the vert at its bot-left corner.
      // face i is the face to the right of the edge directed from this vert to neighbor i
      int bot_left_face = face_index(coord - ivec2(1, 1), samples, num_vertices);
      int bot_right_face = face_index(coord - ivec2(0, 1), samples, num_vertices);
      int top_left_face = face_index(coord - ivec2(1, 0), samples, num_vertices);
      int top_right_face = face_index(coord, samples, num_vertices);
      ivec4 faces(top_right_face, bot_left_face, bot_right_face, top_left_face);

      MorphNode vert_node;
      vert_node.node_type = TYPE_VERTEX;
      vert_node.pos = pos;
      vert_node.neighbors = neighbors;
      vert_node.faces = faces;
      vertex_nodes.push_back(vert_node);

      // record the face quad if applicable. We record a face when processing
      // the vertex in bot-left corner
      if (x < samples[0] - 1 && y < samples[1] - 1) {
        int patch_a = coord_to_index(coord, samples);
        int patch_b = coord_to_index(coord + ivec2(1, 0), samples);
        int patch_c = coord_to_index(coord + ivec2(1, 1), samples);
        int patch_d = coord_to_index(coord + ivec2(0, 1), samples);
        // we store the face's (quad's) verts in the neighbors vec, CCW winding order
        // MorphNode is conceptually a union. The other fields are unused with face type
        MorphNode face_node;
        face_node.node_type = TYPE_FACE;
        face_node.neighbors = ivec4(patch_a, patch_b, patch_c, patch_d);
        face_nodes.push_back(face_node);
      }
    }
  }
  vertex_nodes.insert(vertex_nodes.end(), face_nodes.begin(), face_nodes.end());
  return vertex_nodes;
}

/*
vector<MorphNode> gen_morph_data_old(ivec2 samples) {
  vector<MorphNode> vertex_nodes;
  vector<MorphNode> face_nodes;
  for (int y = 0; y < samples[1]; ++y) {
    for (int x = 0; x < samples[0]; ++x) {
      ivec2 coord(x, y);
      vec3 pos = gen_sphere(vec2(coord) / vec2(samples - 1));

      int upper_neighbor = coord_to_index(coord + ivec2(0, 1), samples);
      int lower_neighbor = coord_to_index(coord + ivec2(0, -1), samples);
      int right_neighbor = coord_to_index(coord + ivec2(1, 0), samples);
      int left_neighbor = coord_to_index(coord + ivec2(-1, 0), samples);
      ivec4 neighbors(upper_neighbor, lower_neighbor, right_neighbor, left_neighbor);

      // the index of the face is the index of the vert at its top-right corner
      int bot_left_face = coord_to_index(coord, samples);
      int bot_right_face = coord_to_index(coord + ivec2(1, 0), samples);
      int top_left_face = coord_to_index(coord + ivec2(0, 1), samples);
      int top_right_face = coord_to_index(coord + ivec2(1, 1), samples);
      // the order of the faces is such that face[i] is to the right of the
      // edge directed from this vert to neighbor[i]
      ivec4 faces(top_right_face, bot_left_face, bot_right_face, top_left_face);

      MorphNode vert_node;
      vert_node.node_type = TYPE_VERTEX;
      vert_node.pos = pos;
      vert_node.neighbors = neighbors;
      vert_node.faces = faces;
      vertex_nodes.push_back(vert_node);

      // record the face quad
      int patch_a = coord_to_index(coord - ivec2(1, 1), samples);
      int patch_b = coord_to_index(coord - ivec2(0, 1), samples);
      int patch_c = coord_to_index(coord - ivec2(1, 0), samples);
      int patch_d = coord_to_index(coord, samples);
      // we store the face's (quad's) verts in the neighbors vec.
      // MorphNode is conceptually a union. The other fields are unused with face type
      MorphNode face_node;
      face_node.node_type = TYPE_FACE;
      // we will ensure that the order is CCW winding later
      face_node.neighbors = ivec4(patch_a, patch_b, patch_d, patch_c);
      face_nodes.push_back(face_node);
    }
  }
  // adjust the order of a face's vertex indices s.t. they are in CCW winding order.
  // uses the fact that we are generating a sphere about the origin.
  // we must allow for the face that at the poles two quad positions may be the same
  for (MorphNode& node : face_nodes) {
    ivec4 indices = node.neighbors;
    vec3 p0 = vertex_nodes[indices[0]].pos;
    vec3 p1 = vertex_nodes[indices[1]].pos;
    vec3 p2 = vertex_nodes[indices[2]].pos;
    vec3 p3 = vertex_nodes[indices[3]].pos;
    vec3 du = p1 - p0;
    vec3 dv = p3 - p0;
    float eps = 1e-12;
    if (dot(du,du) < eps || dot(dv, dv) < eps) {
      // if p0 is at the same pt as p1 or p3, then p2 must not be at the same
      // pt as any other pt of the quad (ow we have a line, not a face)
      du = p3 - p2;
      dv = p1 - p2;
    }
    vec3 nor = cross(du, dv);
    vec3 face_pos = (p0 + p1 + p2 + p3) / 4.0f;
    // if the normal points inwards to the origin, reverse the order of the indices
    if (dot(face_pos, nor) < 0) {
      node.neighbors = ivec4(indices[3], indices[2], indices[1], indices[0]);
    }
  }

  // offset the face indices, since the full array will be [vert_nodes, face_nodes]
  // the face node indices don't require adjustment
  for (MorphNode& node : vertex_nodes) {
    node.faces += vertex_nodes.size();
  }
  vertex_nodes.insert(vertex_nodes.end(), face_nodes.begin(), face_nodes.end());
  return vertex_nodes;
}

vector<MorphNode> gen_sample_data() {
  vector<MorphNode> nodes{3};
  nodes[0].pos = vec3(0.0);
  nodes[0].neighbors = ivec4(0, 1, 2, 3);
  nodes[0].faces = ivec4(0, 1, 2, 3);
  nodes[1].pos = vec3(1.0,0.0,0.0);
  nodes[1].neighbors = ivec4(1);
  nodes[2].pos = vec3(0.0,1.0,0.0);
  return nodes;
}
*/

vector<MorphNode> gen_sample_square() {
  vector<MorphNode> nodes = {
    MorphNode(0, vec3(-0.5,-0.5,0.0), ivec4(3,-1,1,-1), ivec4(5,-1,-1,-1)),
    MorphNode(0, vec3(0.5,-0.5,0.0), ivec4(2,-1,-1,0), ivec4(-1,-1,-1,5)),
    MorphNode(0, vec3(0.5,0.5,0.0), ivec4(0,1,-1,3), ivec4(-1,5,-1,-5)),
    MorphNode(0, vec3(-0.5,0.5,0.0), ivec4(-1,0,2,-1), ivec4(-1,-1,5,-1)),
    MorphNode(1, vec3(0.0), ivec4(0, 1, 2, 3), ivec4(0))
  };
  return nodes;
}

void set_initial_sim_data(GraphicsState& g_state) {
  log_gl_errors("start set_initial_sim_data");

  MorphBuffer& m_buf = g_state.morph_state.buffers[0];

  ivec2 zygote_samples(g_state.controls.num_zygote_samples);
  vector<MorphNode> nodes = gen_morph_data(zygote_samples);
  //vector<MorphNode> nodes = gen_sample_data();
  //vector<MorphNode> nodes = gen_sample_square();
  MorphNodes node_vecs(nodes);

  // log nodes
  if (g_state.controls.log_input_nodes) {
    printf("input nodes:\n");
    for (int i = 0; i < node_vecs.node_type_vec.size(); ++i) {
      MorphNode node = node_vecs.node_at(i);
      printf("%4d %s\n", i, raw_node_str(node).c_str());
    }
    printf("\n\n");
  }

  assert(nodes.size() < MAX_NUM_MORPH_NODES);
  g_state.morph_state.num_nodes = nodes.size();

  write_nodes_to_vbos(m_buf, node_vecs);
  
  log_gl_errors("done set_initial_sim_data");
}

void run_simulation(GraphicsState& g_state, int num_iters) {
  MorphState& m_state = g_state.morph_state;

  MorphProgram& m_prog = m_state.programs[m_state.cur_prog_index];

  glUseProgram(m_prog.gl_handle);
  glValidateProgram(m_prog.gl_handle);
  log_program_info_logs(m_prog.name + ", validate program log",
      m_prog.gl_handle);

  glEnable(GL_RASTERIZER_DISCARD);

  // perform double-buffered iterations
  // assume the initial data is in buffer 0
  printf("starting sim: %d iters, %d nodes\n", num_iters, m_state.num_nodes);
  for (int i = 0; i < num_iters; ++i) {
    MorphBuffer& cur_buf = m_state.buffers[i & 1];
    MorphBuffer& next_buf = m_state.buffers[(i + 1) & 1];

    // set uniforms
    glUniform1i(m_prog.unif_iter_num, i);

    // setup texture buffers and transform feedback buffers
    glBindVertexArray(cur_buf.vao);
    for (int i = 0; i < MORPH_BUF_COUNT; ++i) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_BUFFER, cur_buf.tex_bufs[i]);

      glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, i, next_buf.vbos[i]);
    }

    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, m_state.num_nodes);
    glEndTransformFeedback();
  }
  printf("done sim\n");
  // store the index of the most recently written buffer
  m_state.result_buffer_index = num_iters % 2;

  glDisable(GL_RASTERIZER_DISCARD);

  log_gl_errors("done simulation");
}

void run_simulation_pipeline(GraphicsState& g_state) {
  log_gl_errors("starting generation\n");
  set_initial_sim_data(g_state);
  run_simulation(g_state, g_state.controls.num_iters);
  generate_render_data(g_state);
  log_gl_errors("finished generation\n");
}

void set_sample_render_data(RenderState& r_state) {
  vector<Vertex> vertices = {
    {vec3(0.0), vec3(0.0,0.0,-1.0), vec4(1.0,0.0,0.0,1.0)},
    {vec3(1.0,0.0,0.0), vec3(0.0,0.0,-1.0), vec4(1.0,0.0,0.0,1.0)},
    {vec3(1.0,1.0,0.0), vec3(0.0,0.0,-1.0), vec4(1.0,0.0,0.0,1.0)}
  };
  vector<GLuint> indices = {
    0, 1, 2
  };
  glBindBuffer(GL_ARRAY_BUFFER, r_state.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(GLuint), indices.data());
  r_state.vertex_count = vertices.size();
  r_state.elem_count = indices.size();
}

void render_frame(GraphicsState& g_state) {
  RenderState& r_state = g_state.render_state;
  glUseProgram(r_state.prog);

  // set uniforms
  float aspect_ratio = r_state.fb_width / (float) r_state.fb_height;
  Camera& cam = g_state.camera;
  vec3 eye = vec3(cam.cam_to_world[3]);
  vec3 forward = vec3(cam.cam_to_world[2]);
  vec3 up = vec3(cam.cam_to_world[1]);
  mat4 mv_matrix = glm::lookAt(eye, eye + forward, up);
  mat4 proj_matrix = glm::perspective((float) M_PI / 4.0f, aspect_ratio, 1.0f, 10000.0f);
  glUniformMatrix4fv(r_state.unif_mv_matrix, 1, 0, &mv_matrix[0][0]);
  glUniformMatrix4fv(r_state.unif_proj_matrix, 1, 0, &proj_matrix[0][0]);

  //set_sample_render_data(r_state);

  glBindVertexArray(r_state.vao);
  glPointSize(10.0f);

  if (g_state.controls.render_faces) {
    glUniform1i(r_state.unif_debug_render, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
    glDrawElements(GL_TRIANGLES, r_state.elem_count, GL_UNSIGNED_INT, nullptr);
  }
  vec3 debug_col(1.0,0.0,0.0);
  if (g_state.controls.render_wireframe) {
    glUniform1i(r_state.unif_debug_render, 1);
    glUniform3fv(r_state.unif_debug_color, 1, &debug_col[0]);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
    glDrawElements(GL_TRIANGLES, r_state.elem_count, GL_UNSIGNED_INT, nullptr);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  if (g_state.controls.render_points) {
    glUniform1i(r_state.unif_debug_render, 1);
    glUniform3fv(r_state.unif_debug_color, 1, &debug_col[0]);
    glDrawArrays(GL_POINTS, 0, r_state.vertex_count);
  }

  log_gl_errors("done render_frame");
}

void handle_key_event(GLFWwindow* win, int key, int scancode,
    int action, int mods) {
  GraphicsState* g_state = static_cast<GraphicsState*>(glfwGetWindowUserPointer(win));
  //printf("key event\n");
  if (key == GLFW_KEY_R && action == GLFW_PRESS) {
    // reset camera pos
    g_state->camera = Camera();
  }
}

void update_camera(GLFWwindow* win, Camera& cam) {
  vec3 delta(0.0);
  if (glfwGetKey(win, GLFW_KEY_W)) {
    delta += vec3(0.0,0.0,1.0);
  }
  if (glfwGetKey(win, GLFW_KEY_A)) {
    delta += vec3(1.0,0.0,0.0);
  }
  if (glfwGetKey(win, GLFW_KEY_S)) {
    delta += vec3(0.0,0.0,-1.0);
  }
  if (glfwGetKey(win, GLFW_KEY_D)) {
    delta += vec3(-1.0,0.0,0.0);
  }
  if (glfwGetKey(win, GLFW_KEY_Q)) {
    delta += vec3(0.0,-1.0,0.0);
  }
  if (glfwGetKey(win, GLFW_KEY_E)) {
    delta += vec3(0.0,1.0,0.0);
  }
  vec3 trans = delta * 5.0f * 0.017f;
  mat4 trans_mat = glm::translate(mat4(1.0), trans);

  mat4 rot_mat(1.0);
  float amt_degs = 0.017 * 2.0f;
  if (glfwGetKey(win, GLFW_KEY_LEFT)) {
    rot_mat = glm::rotate(mat4(1.0), amt_degs, vec3(0.0,1.0,0.0));
  } else if (glfwGetKey(win, GLFW_KEY_RIGHT)) {
    rot_mat = glm::rotate(mat4(1.0), -amt_degs, vec3(0.0,1.0,0.0));
  } else if (glfwGetKey(win, GLFW_KEY_UP)) {
    rot_mat = glm::rotate(mat4(1.0), amt_degs, vec3(1.0,0.0,0.0));
  } else if (glfwGetKey(win, GLFW_KEY_DOWN)) {
    rot_mat = glm::rotate(mat4(1.0), -amt_degs, vec3(1.0,0.0,0.0));
  }
  
  cam.cam_to_world = cam.cam_to_world * rot_mat * trans_mat;
}

void run_app() {
  signal(SIGSEGV, handle_segfault);
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  int target_width = 1300;
  int target_height = 700;
  GLFWwindow* window = glfwCreateWindow(target_width, target_height, "morph", NULL, NULL);
  if (!window) {
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  glfwMakeContextCurrent(window);
  gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
  glfwSwapInterval(1);

  printf("OpenGL: %d.%d\n", GLVersion.major, GLVersion.minor);

  GraphicsState g_state;
  setup_opengl(g_state);

  glfwSetWindowUserPointer(window, &g_state);
  glfwSetKeyCallback(window, handle_key_event);

  // setup imgui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  ImGui::StyleColorsDark();
  //ImGui::StyleColorsClassic();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  const char* glsl_version = "#version 150";
  ImGui_ImplOpenGL3_Init(glsl_version);

  bool requires_mac_mojave_fix = true;
  bool show_dev_console = true;
  // for maintaining the fps
  float fps = 60;
  auto start_of_frame = chrono::steady_clock::now();
  chrono::milliseconds frame_duration{(int) (1000.0f / fps)};
  // for logging the fps
  int fps_stat = 0;
  int frame_counter = 0;
  auto start_of_sec = chrono::steady_clock::now();

  while (!glfwWindowShouldClose(window)) {
    std::this_thread::sleep_until(start_of_frame);
    start_of_frame += frame_duration;

    // for logging the fps
    frame_counter += 1;
    if (start_of_sec < chrono::steady_clock::now()) {
      start_of_sec += chrono::seconds(1);
      fps_stat = frame_counter;
      frame_counter = 0;
    }

    glfwPollEvents();
    update_camera(window, g_state.camera);

    // the screen appears black on mojave until a resize occurs
    if (requires_mac_mojave_fix) {
      requires_mac_mojave_fix = false;
      glfwSetWindowSize(window, target_width, target_height + 1);
    }
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("dev console", &show_dev_console);
    ImGui::Text("fps: %d", fps_stat);

    Controls& controls = g_state.controls;
    ImGui::Separator();
    ImGui::Text("render controls:");
    ImGui::Checkbox("render faces", &controls.render_faces);
    ImGui::Checkbox("render points", &controls.render_points);
    ImGui::Checkbox("render wireframe", &controls.render_wireframe);

    ImGui::Separator();
    ImGui::Text("simulation controls:");
    ImGui::Checkbox("log input nodes", &controls.log_input_nodes);
    ImGui::Checkbox("log output nodes", &controls.log_output_nodes);
    ImGui::Checkbox("log render data", &controls.log_render_data);
    vector<const char*> prog_names;
    for (auto& prog : g_state.morph_state.programs) {
      prog_names.push_back(prog.name.c_str());
    }
    ImGui::Combo("program", &g_state.morph_state.cur_prog_index,
        prog_names.data(), prog_names.size());
    ImGui::InputInt("num iters", &controls.num_iters);
    controls.num_iters = std::max(controls.num_iters, 0);
    ImGui::InputInt("AxA samples", &controls.num_zygote_samples);
    controls.num_zygote_samples = std::max(controls.num_zygote_samples, 0);
    if (ImGui::Button("run simulation")) {
      run_simulation_pipeline(g_state);  
    }

    ImGui::End();

    ImGui::Render();
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    
    glViewport(0, 0, fb_width, fb_height);
    glClearColor(1, 1, 1, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    g_state.render_state.fb_width = fb_width;
    g_state.render_state.fb_height = fb_height;

    render_frame(g_state);
    
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
}
