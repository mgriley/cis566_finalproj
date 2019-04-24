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
#include <unordered_map>
#include <sstream>
#include <fstream>
#include "shaders/render_shaders.h"
#include "shaders/morph_shaders.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

using namespace std;
using namespace glm;

string vec3_str(vec3 v) {
  char s[100];
  sprintf(s, "[%.2f, %.2f, %.2f]", v[0], v[1], v[2]);
  return string(s);
}

struct Camera {
  mat4 cam_to_world;
  Camera();
};

Camera::Camera() {
  vec3 forward(0.0,0.0,1.0);
  vec3 up(0.0,1.0,0.0);
  vec3 right = cross(up, forward);
  vec3 pos(0.0,0.0,-10.0);

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

  GLuint unif_mv_matrix;
  GLuint unif_proj_matrix;

  GLuint pos_attrib;
  GLuint nor_attrib;
  GLuint col_attrib;
};

// For use in the interleaved transform feedback buffer
enum MorphNodeType {
  TYPE_VERTEX = 0,
  TYPE_FACE
};

struct MorphNode {
  int node_type;
  vec3 pos;
  ivec4 neighbors;
  ivec4 faces;

  MorphNode();
};

MorphNode::MorphNode():
  node_type(TYPE_VERTEX),
  pos(0.0),
  neighbors(-1),
  faces(-1)
{
}

string node_str(MorphNode const& node) {
  array<char, 200> s;
  if (node.node_type == TYPE_VERTEX) {
    sprintf(s.data(), "VERT pos: %s, neighbors: %s, faces: %s",
        vec3_str(node.pos).c_str(), to_string(node.neighbors).c_str(),
        to_string(node.faces).c_str());
  } else {
    sprintf(s.data(), "FACE verts: %s", to_string(node.neighbors).c_str());
  }
  return string(s.data());
}

struct MorphBuffer {
  GLuint vao;
  GLuint vbo;
  GLuint tex_buf;
};

struct MorphState {
  GLuint prog;
  array<MorphBuffer, 2> buffers;
  // set to the index of the buffer that holds
  // the most recent simulation result
  int result_buffer_index;

  GLuint node_type_attrib;
  GLuint pos_attrib;
  GLuint neighbors_attrib;
  GLuint faces_attrib;

  // the number of nodes used in the most recent sim
  int num_nodes;
};

struct GraphicsState {
  Camera camera;
  RenderState render_state;
  MorphState morph_state;
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
  const GLubyte* vendor = glGetString(GL_VENDOR);
  const GLubyte* renderer = glGetString(GL_RENDERER);
  const GLubyte* version = glGetString(GL_VERSION);
  const GLubyte* glsl_version = glGetString(GL_SHADING_LANGUAGE_VERSION);
  printf("vendor: %s\nrenderer: %s\nversion: %s\nglsl version: %s\n",
      vendor, renderer, version, glsl_version);  
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
        varyings.data(), GL_INTERLEAVED_ATTRIBS);
  }

  glLinkProgram(program);
  glValidateProgram(program);
  log_program_info_logs(prog_name + ", program log", program);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  log_gl_errors("setup program");

  return program;
}

// Size in bytes of the respective buffers
// TODO - check these later
const GLsizeiptr RENDER_VBO_SIZE = (GLsizeiptr) 1e8;
const GLsizeiptr RENDER_INDEX_BUFFER_SIZE = (GLsizeiptr) 1e8;

void configure_render_program(GraphicsState& g_state) {
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
  assert(r_state.unif_mv_matrix != -1);
  assert(r_state.unif_proj_matrix != -1);

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

// TODO - check this later.
// The maximum # of morph nodes
const int MAX_NUM_MORPH_NODES = 10;//(int) (1e8 / (float) sizeof(MorphNode));

void configure_morph_program(GraphicsState& g_state) {
  MorphState& m_state = g_state.morph_state;

  m_state.prog = setup_program(
      "morph program", MORPH_VERTEX_SRC, MORPH_FRAGMENT_SRC, true);
  
  m_state.node_type_attrib = glGetAttribLocation(m_state.prog, "node_type");
  m_state.pos_attrib = glGetAttribLocation(m_state.prog, "pos");
  m_state.neighbors_attrib = glGetAttribLocation(m_state.prog, "neighbors");
  m_state.faces_attrib = glGetAttribLocation(m_state.prog, "faces");
  vector<GLuint> all_attribs = {m_state.node_type_attrib, m_state.pos_attrib,
    m_state.neighbors_attrib, m_state.faces_attrib};
  for (GLuint attrib : all_attribs) {
    assert(attrib != -1);
  }

  for (MorphBuffer& m_buf : m_state.buffers) {
    glGenVertexArrays(1, &m_buf.vao);
    glBindVertexArray(m_buf.vao);

    glGenBuffers(1, &m_buf.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_buf.vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_NUM_MORPH_NODES * sizeof(MorphNode), nullptr, GL_DYNAMIC_COPY);

    // setup attributes

    glVertexAttribIPointer(m_state.node_type_attrib, 1, GL_INT,
        sizeof(MorphNode), (void*) offsetof(MorphNode, node_type));
    glVertexAttribPointer(m_state.pos_attrib, 3, GL_FLOAT, GL_FALSE,
        sizeof(MorphNode), (void*) offsetof(MorphNode, pos));
    glVertexAttribIPointer(m_state.neighbors_attrib, 4, GL_INT,
        sizeof(MorphNode), (void*) offsetof(MorphNode, neighbors));
    glVertexAttribIPointer(m_state.faces_attrib, 4, GL_INT,
        sizeof(MorphNode), (void*) offsetof(MorphNode, faces));

    for (GLuint attrib : all_attribs) {
      glEnableVertexAttribArray(attrib);
    }

    glGenTextures(1, &m_buf.tex_buf);
    glBindTexture(GL_TEXTURE_BUFFER, m_buf.tex_buf);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8, m_buf.vbo);
  }
}

void setup_opengl(GraphicsState& state) {
  log_opengl_info();
  
  configure_render_program(state);
  configure_morph_program(state);

  log_gl_errors("done setup_opengl");
}

// Use the simulation data to generate render data
void generate_render_data(GraphicsState& g_state) {  
  MorphState& m_state = g_state.morph_state;
  MorphBuffer& target_buf = m_state.buffers[m_state.result_buffer_index];
  glBindVertexArray(target_buf.vao);
  glBindBuffer(GL_ARRAY_BUFFER, target_buf.vbo);

  // read Node data from GPU
  vector<MorphNode> nodes{(size_t) m_state.num_nodes};
  glGetBufferSubData(GL_ARRAY_BUFFER, 0, m_state.num_nodes * sizeof(MorphNode), nodes.data());
  
  // log nodes
  printf("num nodes: %lu\n", nodes.size()); 
  for (int i = 0; i < nodes.size(); ++i) {
    MorphNode& node = nodes[i];
    printf("%4d %s\n", i, node_str(node).c_str());
  }

  // write Node data to render buffers
  
  vector<Vertex> vertices;
  vector<GLuint> indices;

  // map from index in nodes vector to index in vertices vector
  unordered_map<int, int> redirect_map;

  for (int i = 0; i < nodes.size(); ++i) {
    MorphNode& node = nodes[i];
    if (node.node_type == TYPE_FACE) {
      GLuint index_offset = vertices.size();
      vector<MorphNode> face_nodes;
      for (int j = 0; j < 4; ++j) {
        int node_index = node.neighbors[j];
        face_nodes.push_back(nodes[node_index]);
      }
      // TODO - calc the normal from 3 positions
      vec3 nor(0.0,1.0,0.0);
      for (auto& node : face_nodes) {
        Vertex vertex;
        vertex.pos = node.pos;
        vertex.nor = nor;
        vertex.col = vec4(0.0,1.0,0.0,1.0);
        vertices.push_back(vertex);
      }
      // quad to 2 triangles
      vector<GLuint> new_indices = {
        index_offset, index_offset + 1, index_offset + 2,
        index_offset, index_offset + 2, index_offset + 3
      };
      indices.insert(indices.end(),
          new_indices.begin(), new_indices.end());
    }
  }

  RenderState& r_state = g_state.render_state;
  glBindBuffer(GL_ARRAY_BUFFER, r_state.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(GLuint), indices.data());
}

vec3 gen_sphere(vec2 unit) {
  float v_angle = unit[1] * M_PI;
  float h_angle = unit[0] * 2.0 * M_PI;
  return vec3(
      sin(v_angle) * cos(h_angle),
      sin(v_angle) * sin(h_angle),
      cos(v_angle));
}

// coord to vertex index
int coord_to_index(ivec2 coord, ivec2 samples) {
  return (coord[0] % samples[0]) + samples[0] * (coord[1] % samples[1]);
}

vector<MorphNode> gen_morph_data() {
  ivec2 samples(4, 4);
  // TODO - can optimize may using a single vector of size 2*sx*sy
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
      // use CCW winding order
      face_node.neighbors = ivec4(patch_a, patch_b, patch_d, patch_c);
      face_nodes.push_back(face_node);
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

void set_initial_sim_data(GraphicsState& g_state) {
  MorphBuffer& m_buf = g_state.morph_state.buffers[0];

  // TODO
  vector<MorphNode> nodes = gen_morph_data();
  //vector<MorphNode> nodes = gen_sample_data();

  glBindBuffer(GL_ARRAY_BUFFER, m_buf.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, nodes.size() * sizeof(MorphNode), nodes.data());
  g_state.morph_state.num_nodes = nodes.size();
}

void run_simulation(GraphicsState& g_state, int num_iters) {
  MorphState& m_state = g_state.morph_state;

  glUseProgram(m_state.prog);
  glEnable(GL_RASTERIZER_DISCARD);

  // perform double-buffered iterations
  // assume the initial data is in buffer 0
  for (int i = 0; i < num_iters; ++i) {
    MorphBuffer& cur_buf = m_state.buffers[i & 1];
    MorphBuffer& next_buf = m_state.buffers[(i + 1) & 1];

    glBindVertexArray(cur_buf.vao);
    glBindTexture(GL_TEXTURE_BUFFER, cur_buf.tex_buf);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, next_buf.vbo);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, m_state.num_nodes);
    glEndTransformFeedback();
  }
  // store the index of the most recently written buffer
  m_state.result_buffer_index = num_iters % 2;

  glDisable(GL_RASTERIZER_DISCARD);
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

  glBindVertexArray(r_state.vao);
  //set_sample_render_data(r_state);

  glPointSize(10.0f);
  glDrawArrays(GL_POINTS, 0, 3);
  
  //glDrawElements(GL_TRIANGLES, r_state.elem_count, GL_UNSIGNED_INT, nullptr);

  log_gl_errors("done render_frame");
}

void handle_key_event(GLFWwindow* win, int key, int scancode,
    int action, int mods) {
  GraphicsState* g_state = static_cast<GraphicsState*>(glfwGetWindowUserPointer(win));
  //printf("key event\n");
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

  // TODO - make a simple UI for this
  set_initial_sim_data(g_state);
  run_simulation(g_state, 1);
  generate_render_data(g_state);

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
