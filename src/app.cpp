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
#include <sstream>
#include <fstream>
#include "shaders/render_shaders.h"
#include "shaders/morph_shaders.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

using namespace std;
using namespace glm;

// For use in the interleaved buffer
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

  GLuint unif_mv_matrix;
  GLuint unif_proj_matrix;

  GLuint pos_attrib;
  GLuint nor_attrib;
  GLuint col_attrib;
};

struct MorphNode {
  vec3 pos;
};

struct MorphBuffer {
  GLuint vao;
  GLuint vbo;
  GLuint tex_buf;
};

struct MorphState {
  GLuint prog;
  array<MorphBuffer, 2> buffers;
  GLuint pos_attrib;

  // the number of nodes used in the most recent sim
  int num_nodes;
};

struct GraphicsState {
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
    vector<const char*> varyings = {"out_pos"};
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
const int NUM_MORPH_NODES = 10;//(int) (1e8 / (float) sizeof(MorphNode));

void configure_morph_program(GraphicsState& g_state) {
  MorphState& m_state = g_state.morph_state;

  m_state.prog = setup_program(
      "morph program", MORPH_VERTEX_SRC, MORPH_FRAGMENT_SRC, true);
  
  m_state.pos_attrib = glGetAttribLocation(m_state.prog, "pos");
  assert(m_state.pos_attrib != -1);

  for (MorphBuffer& m_buf : m_state.buffers) {
    glGenVertexArrays(1, &m_buf.vao);
    glBindVertexArray(m_buf.vao);

    glGenBuffers(1, &m_buf.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_buf.vbo);
    glBufferData(GL_ARRAY_BUFFER, NUM_MORPH_NODES * sizeof(MorphNode), nullptr, GL_DYNAMIC_COPY);

    glVertexAttribPointer(m_state.pos_attrib, 3, GL_FLOAT, GL_FALSE,
        sizeof(MorphNode), (void*) offsetof(MorphNode, pos));
    glEnableVertexAttribArray(m_state.pos_attrib);

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
  MorphBuffer& target_buf = m_state.buffers[0];
  glBindVertexArray(target_buf.vao);
  glBindBuffer(GL_ARRAY_BUFFER, target_buf.vbo);

  vector<MorphNode> nodes{(size_t) m_state.num_nodes};
  glGetBufferSubData(GL_ARRAY_BUFFER, 0, m_state.num_nodes * sizeof(MorphNode), nodes.data());
  
  for (MorphNode& node : nodes) {
    printf("node pos x: %f\n", node.pos.x);
  }
}

void set_initial_sim_data(GraphicsState& g_state) {
  MorphBuffer& m_buf = g_state.morph_state.buffers[0];

  vector<MorphNode> nodes = {
    {vec3(1.0)},
    {vec3(2.0)},
    {vec3(3.0)}
  };
  glBindBuffer(GL_ARRAY_BUFFER, m_buf.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, nodes.size() * sizeof(MorphNode), nodes.data());
  g_state.morph_state.num_nodes = nodes.size();
}

void run_simulation(GraphicsState& g_state, int num_iters) {
  MorphState& m_state = g_state.morph_state;

  glUseProgram(m_state.prog);
  glEnable(GL_RASTERIZER_DISCARD);

  // perform double-buffered iterations
  // assume the initial data is in buffer 0, and make num_iters even
  // so the result ends in buffer 0
  num_iters += num_iters % 2;
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

  glDisable(GL_RASTERIZER_DISCARD);
}

int set_sample_render_data(RenderState& r_state) {
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
  return indices.size();
}

void render_frame(GraphicsState& g_state) {
  RenderState& r_state = g_state.render_state;
  glUseProgram(r_state.prog);

  // set uniforms
  float aspect_ratio = r_state.fb_width / (float) r_state.fb_height;
  vec3 eye(0.0,0.0,-10.0);
  vec3 center(0.0);
  vec3 up(0.0,1.0,0.0);
  mat4 mv_matrix = glm::lookAt(eye, center, up);
  mat4 proj_matrix = glm::perspective((float) M_PI / 4.0f, aspect_ratio, 1.0f, 10000.0f);
  glUniformMatrix4fv(r_state.unif_mv_matrix, 1, 0, &mv_matrix[0][0]);
  glUniformMatrix4fv(r_state.unif_proj_matrix, 1, 0, &proj_matrix[0][0]);

  glBindVertexArray(r_state.vao);
  //int elem_count = set_sample_render_data(r_state);

  glPointSize(10.0f);
  glDrawArrays(GL_POINTS, 0, 3);
  
  //glDrawElements(GL_TRIANGLES, elem_count, GL_UNSIGNED_INT, nullptr);

  log_gl_errors("done render_frame");
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
