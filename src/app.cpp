#include "app.h"
#include "utils.h"
#include "types.h"
#include "dummy_morph_shaders.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <chrono>
#include <thread>
#include <utility>
#include <tuple>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <fstream>

using namespace std;
using namespace glm;

// Size in bytes of the respective buffers
// TODO - check these later
const GLsizeiptr RENDER_VBO_SIZE = (GLsizeiptr) 1e8;
const GLsizeiptr RENDER_INDEX_BUFFER_SIZE = (GLsizeiptr) 1e8;

// The maximum # of morph nodes
const int MAX_NUM_MORPH_NODES = (int) (1e8 / (float) sizeof(MorphNode));

// NOTE: you must put the names of your morph and render shaders here
const vector<string> morph_shader_files = {
  "growth.glsl",
};
const pair<string, string> render_shader_files = {
  "render_vert.glsl", "render_frag.glsl"
};

void glfw_error_callback(int error_code, const char* error_msg) {
  printf("GLFW error: %d, %s", error_code, error_msg);
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

  log_gl_errors("before varyings, setup program");
  if (is_morph_prog) {
    vector<const char*> varyings = {
      "out_pos",
      "out_vel",
      "out_neighbors",
      "out_data"
    };
    glTransformFeedbackVaryings(program, varyings.size(),
        varyings.data(), GL_SEPARATE_ATTRIBS);
  }

  glLinkProgram(program);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  log_gl_errors(prog_name + " (setup program)");

  return program;
}

void setup_user_uniforms(const char* prog_name, GLuint prog, vector<UserUnif>& user_unifs) {
  // setup user-defined unifs
  for (UserUnif& user_unif : user_unifs) {
    user_unif.gl_handle = glGetUniformLocation(
        prog, user_unif.name.c_str());
    if (user_unif.gl_handle == -1) {
      printf("%s: WARNING user unif \"%s\" is -1\n", prog_name, user_unif.name.c_str());
    }
  }
}

void add_morph_program(MorphState& m_state, const char* prog_name,
    const char* vertex_src, vector<UserUnif>& user_unifs) {
  MorphProgram prog(prog_name, user_unifs);
  prog.gl_handle = setup_program(
      prog_name, vertex_src, MORPH_DUMMY_FRAGMENT_SRC, true);
  
  // Note: we do not need to get the input attribute locations
  // b/c they are explicit in each program

  // setup uniforms
  glUseProgram(prog.gl_handle);
  setup_user_uniforms(prog_name, prog.gl_handle, prog.user_unifs); 
  prog.unif_iter_num = glGetUniformLocation(
      prog.gl_handle, "iter_num");
  if (prog.unif_iter_num == -1) {
    printf("%s: WARNING iter_num is -1\n", prog_name);
  }
  prog.unif_num_nodes = glGetUniformLocation(
      prog.gl_handle, "num_nodes");
  if (prog.unif_num_nodes == -1) {
    printf("%s: WARNING num_nodes is -1\n", prog_name);
  }

  // setup texture buffer samplers
  vector<const char*> unif_sampler_names = {
    "pos_buf", "vel_buf", "neighbors_buf", "data_buf"
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

  // add the program or replace it if it already exists
  bool existing_prog = false;
  for (int i = 0; i < m_state.programs.size(); ++i) {
    if (m_state.programs[i].name == prog.name) {
      existing_prog = true;
      m_state.programs[i] = prog;
      break;
    }
  }
  if (!existing_prog) {
    m_state.programs.push_back(prog);
  }
}

void set_render_program(RenderState& r_state, const char* prog_name,
    const char* vertex_src, const char* frag_src, vector<UserUnif>& user_unifs) {
  RenderProgram prog(prog_name, user_unifs);
  prog.gl_handle = setup_program(
      prog_name, vertex_src, frag_src, false);

  // setup uniforms
  glUseProgram(prog.gl_handle);
  setup_user_uniforms(prog_name, prog.gl_handle, prog.user_unifs);
  prog.unif_mv_matrix = glGetUniformLocation(
      prog.gl_handle, "mv_matrix");
  prog.unif_proj_matrix = glGetUniformLocation(
      prog.gl_handle, "proj_matrix");
  prog.unif_debug_render = glGetUniformLocation(
      prog.gl_handle, "debug_render");
  prog.unif_debug_color = glGetUniformLocation(
      prog.gl_handle, "debug_color");
  /*
  assert(prog.unif_mv_matrix != -1);
  assert(prog.unif_proj_matrix != -1);
  assert(prog.unif_debug_render != -1);
  assert(prog.unif_debug_color != -1);
  */

  // update the render program
  r_state.prog = prog;
}

void read_prog_file(string prog_filename,
    string& out_prog_text, vector<UserUnif>& out_user_unifs) {
  FILE* prog_file = fopen(prog_filename.c_str(), "r");
  if (!prog_file) {
    printf("WARNING shader file not found at the path:\n%s\n", prog_filename.c_str());
  }
  // read user defined uniforms from the file header
  vector<UserUnif> user_unifs;
  while (true) {
    array<char, 100> unif_name;
    int num_comps = 0;
    float min = 0.0;
    float max = 1.0;
    float drag_speed = 1.0;
    vec4 def_val(0.0);

    int num_read = fscanf(prog_file, "%s", unif_name.data());
    assert(num_read == 1);
    if (strcmp(unif_name.data(), "END_USER_UNIFS") == 0) {
      break;    
    }
    num_read = fscanf(prog_file,
        " comps %d min %f max %f speed %f default ",
        &num_comps, &min, &max, &drag_speed);
    assert(num_read == 4);
    for (int i = 0; i < num_comps; ++i) {
      num_read = fscanf(prog_file, "%f", &def_val[i]);
      assert(num_read == 1);
    }
    UserUnif unif(unif_name.data(), num_comps,
        min, max, drag_speed, def_val, def_val);
    user_unifs.push_back(unif);
  }
  // read the GLSL text
  long start_pos = ftell(prog_file);
  fseek(prog_file, 0, SEEK_END);
  long glsl_len = ftell(prog_file) - start_pos;
  fseek(prog_file, start_pos, SEEK_SET);
  array<char, 10000> prog_buf;
  assert(glsl_len < prog_buf.size());
  int bytes_read = fread(prog_buf.data(), 1, glsl_len, prog_file);
  fclose(prog_file);
  assert(bytes_read == glsl_len);
  prog_buf[glsl_len] = '\0';

  out_prog_text = string(prog_buf.data());
  out_user_unifs = user_unifs;
}

void load_render_program(GraphicsState& g_state) {
  // Note that the user must specify all tunable unifs in the vertex
  // source file. The ones in the fragment file will be disregarded.

  string vert_text;  
  vector<UserUnif> vert_unifs;
  string vert_filename = g_state.base_shader_path +
    "/shaders/" + render_shader_files.first;
  read_prog_file(vert_filename, vert_text, vert_unifs);
  
  string frag_text;
  vector<UserUnif> frag_unifs;
  string frag_filename = g_state.base_shader_path +
    "/shaders/" + render_shader_files.second;
  read_prog_file(frag_filename, frag_text, frag_unifs);

  set_render_program(g_state.render_state, render_shader_files.first.c_str(),
    vert_text.c_str(), frag_text.c_str(), vert_unifs);
}

void load_morph_program(GraphicsState& g_state, string prog_name) {
  MorphState& m_state = g_state.morph_state;
  string prog_text;
  vector<UserUnif> user_unifs;
  string prog_filename = g_state.base_shader_path + "/shaders/" + prog_name;
  read_prog_file(prog_filename, prog_text, user_unifs);
  add_morph_program(m_state, prog_name.c_str(), prog_text.c_str(), user_unifs);
}

void reload_current_programs(GraphicsState& g_state) {
  // reload the current morph program and the render program
  MorphState& m_state = g_state.morph_state;
  MorphProgram& cur_prog = m_state.programs[m_state.cur_prog_index];
  load_morph_program(g_state, cur_prog.name);

  load_render_program(g_state);
}

void load_all_morph_programs(GraphicsState& g_state) {
  g_state.morph_state.programs.clear();
  for (string prog_name : morph_shader_files) {
    load_morph_program(g_state, prog_name);    
  }
}

void init_morph_state(GraphicsState& g_state) {
  MorphState& m_state = g_state.morph_state;

  load_all_morph_programs(g_state);

  // Setup the VAO and other state for each of the two buffers
  // used for double-buffering
  for (MorphBuffer& m_buf : m_state.buffers) {
    glGenVertexArrays(1, &m_buf.vao);
    glBindVertexArray(m_buf.vao);

    // setup VBOs
    // each tuple is {sizeof element, components per element,
    // internal format of component, is integer format}
    vector<tuple<int, int, GLenum, bool>> vbo_params = {
      {sizeof(vec4), 4, GL_FLOAT, false},
      {sizeof(vec4), 4, GL_FLOAT, false},
      {sizeof(vec4), 4, GL_FLOAT, false},
      {sizeof(vec4), 4, GL_FLOAT, false},
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
      GL_RGBA32F, GL_RGBA32F, GL_RGBA32F, GL_RGBA32F
    };
    for (int i = 0; i < m_buf.tex_bufs.size(); ++i) {
      glBindTexture(GL_TEXTURE_BUFFER, m_buf.tex_bufs[i]);
      glTexBuffer(GL_TEXTURE_BUFFER, internal_formats[i], m_buf.vbos[i]);
    }
  }
}

void init_render_state(GraphicsState& g_state) {
  RenderState& r_state = g_state.render_state;

  load_render_program(g_state);

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

  // interleave the attributes within the single VBO
  // Note that this is independent of shader program since
  // we the shader program must explicitly set the attribute locations

  // each element is {num float components, offset}
  vector<pair<int, void*>> attrib_params = {
    {4, (void*) offsetof(Vertex, pos)},
    {4, (void*) offsetof(Vertex, vel)},
    {4, (void*) offsetof(Vertex, data)},
    {3, (void*) offsetof(Vertex, nor)}
  };
  assert(RENDER_ATTRIB_COUNT == attrib_params.size());
  for (int i = 0; i < RENDER_ATTRIB_COUNT; ++i) {
    glVertexAttribPointer(i, attrib_params[i].first, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), attrib_params[i].second);
    glEnableVertexAttribArray(i);
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
    {sizeof(vec4), node_vecs.pos_vec.data()},
    {sizeof(vec4), node_vecs.vel_vec.data()},
    {sizeof(vec4), node_vecs.neighbors_vec.data()},
    {sizeof(vec4), node_vecs.data_vec.data()},
  };
  int num_nodes = node_vecs.pos_vec.size();
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
    {sizeof(vec4), node_vecs.pos_vec.data()},
    {sizeof(vec4), node_vecs.vel_vec.data()},
    {sizeof(vec4), node_vecs.neighbors_vec.data()},
    {sizeof(vec4), node_vecs.data_vec.data()},
  };
  glBindVertexArray(target_buf.vao);
  for (int i = 0; i < target_buf.vbos.size(); ++i) {
    glBindBuffer(GL_ARRAY_BUFFER, target_buf.vbos[i]);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0,
      m_state.num_nodes * data_params[i].first, data_params[i].second);
  }
  return node_vecs;
}

// Helper for gen_render_data
// The node indices must be given in CCW winding order
void add_triangle_face(MorphNodes& node_vecs,
    vector<Vertex>& vertices, vector<GLuint>& indices,
    int i_a, int i_b, int i_c) {
  
  // calc the normal from the face positions.
  vec3 p_a = vec3(node_vecs.pos_vec[i_a]);
  vec3 p_b = vec3(node_vecs.pos_vec[i_b]);
  vec3 p_c = vec3(node_vecs.pos_vec[i_c]);
  vec3 nor = cross(p_b - p_a, p_c - p_a);
  if (dot(nor, nor) < 1e-12) {
    // the face has no area, so skip it
    return;
  }
  nor = normalize(nor);
  GLuint index_offset = vertices.size();
  vector<int> tri_indices = {i_a, i_b, i_c};
  for (int vert_index : tri_indices) {
    vec4 pos = node_vecs.pos_vec[vert_index];
    vec4 vel = node_vecs.vel_vec[vert_index];
    vec4 data = node_vecs.data_vec[vert_index];
    Vertex vertex(pos, vel, data, nor);
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
void gen_render_data(GraphicsState& g_state, MorphNodes& node_vecs) {  
  MorphState& m_state = g_state.morph_state;
    
  if (g_state.controls.log_output_nodes) {
    // log nodes
    printf("output nodes (%d):\n", m_state.num_nodes); 
    for (int i = 0; i < node_vecs.pos_vec.size(); ++i) {
      MorphNode node = node_vecs.node_at(i);
      printf("%4d %s\n", i, raw_node_str(node).c_str());
    }
    printf("\n\n");
  }

  // convert the Node data to render data
  
  vector<Vertex> vertices;
  vector<GLuint> indices;
  for (int i = 0; i < node_vecs.pos_vec.size(); ++i) {
    vec4 neighbors = node_vecs.neighbors_vec[i];
    int i_a = i;
    int i_b = (int) neighbors[0];
    int i_c = (int) node_vecs.neighbors_vec[i_b][1];
    int i_d = (int) neighbors[1];
    if (i_b != -1 && i_d != -1) {
      assert(i_c != -1);
      add_triangle_face(node_vecs, vertices, indices, i_a, i_b, i_c);
      add_triangle_face(node_vecs, vertices, indices, i_a, i_c, i_d);
    }
  }

  if (g_state.controls.log_render_data) {
    // log render data
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
  vec2 plane_pos = 10.0f * (unit - 0.5f);
  return vec3(plane_pos[0], 0.0f, plane_pos[1]);
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

vector<MorphNode> gen_morph_data(ivec2 samples) {
  vector<MorphNode> vertex_nodes;
  vertex_nodes.reserve(samples[0] * samples[1]);
  for (int y = 0; y < samples[1]; ++y) {
    for (int x = 0; x < samples[0]; ++x) {
      ivec2 coord(x, y);
      vec3 pos = gen_plane(vec2(coord) / vec2(samples - 1));

      int upper_neighbor = coord_to_index(coord + ivec2(0, 1), samples);
      int lower_neighbor = coord_to_index(coord + ivec2(0, -1), samples);
      int right_neighbor = coord_to_index(coord + ivec2(1, 0), samples);
      int left_neighbor = coord_to_index(coord + ivec2(-1, 0), samples);
      vec4 neighbors((float) right_neighbor, (float) upper_neighbor, (float) left_neighbor, (float) lower_neighbor);
      
      MorphNode vert_node(vec4(pos, 0.0), vec4(0.0), neighbors, vec4(0.0));
      vertex_nodes.push_back(vert_node);
    }
  }
  return vertex_nodes;
}

vector<MorphNode> gen_sample_square() {
  vector<MorphNode> nodes = {
    MorphNode(vec4(-0.5,-0.5,0.0,0.0), vec4(0.0), vec4(ivec4(3,-1,1,-1)), vec4(0.0)),
    MorphNode(vec4(0.5,-0.5,0.0,0.0), vec4(0.0), vec4(ivec4(2,-1,-1,0)), vec4(0.0)),
    MorphNode(vec4(0.5,0.5,0.0,0.0), vec4(0.0), vec4(ivec4(0,1,-1,3)), vec4(0.0)),
    MorphNode(vec4(-0.5,0.5,0.0,0.0), vec4(0.0), vec4(ivec4(-1,0,2,-1)), vec4(0.0)),
    MorphNode(vec4(0.0), vec4(0.0), vec4(ivec4(0, 1, 2, 3)), vec4(0.0))
  };
  return nodes;
}

void set_initial_sim_data(GraphicsState& g_state) {
  log_gl_errors("start set_initial_sim_data");

  MorphBuffer& m_buf = g_state.morph_state.buffers[0];

  ivec2 zygote_samples(g_state.controls.num_zygote_samples);
  vector<MorphNode> nodes = gen_morph_data(zygote_samples);
  //vector<MorphNode> nodes = gen_sample_square();
  MorphNodes node_vecs(nodes);

  // log nodes
  if (g_state.controls.log_input_nodes) {
    printf("input nodes:\n");
    for (int i = 0; i < node_vecs.pos_vec.size(); ++i) {
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

  // Set uniforms
  for (UserUnif& user_unif : m_prog.user_unifs) {
    glUniform4fv(user_unif.gl_handle, 1, &user_unif.cur_val[0]);
  }
  glUniform1i(m_prog.unif_num_nodes, m_state.num_nodes);

  // perform double-buffered iterations
  // assume the initial data is in buffer 0
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
  // store the index of the most recently written buffer
  m_state.result_buffer_index = num_iters % 2;

  glDisable(GL_RASTERIZER_DISCARD);

  log_gl_errors("done simulation");
}

void update_ui_for_output(GraphicsState& g_state, MorphNodes& node_vecs) {
  vec3 furthest_pos(0.0);
  for (int i = 0; i < node_vecs.pos_vec.size(); ++i) {
    if (length(node_vecs.pos_vec[i]) > length(furthest_pos)) {
      furthest_pos = node_vecs.pos_vec[i];
    }
  }
  Camera far_cam;
  far_cam.set_view(
      2.0f*length(furthest_pos)*normalize(vec3(0.0,2.0,-2.0)), vec3(0.0));
  g_state.controls.zoom_to_fit_cam = far_cam;
}

void run_simulation_pipeline(GraphicsState& g_state) {
  log_gl_errors("starting sim pipeline\n");
  set_initial_sim_data(g_state);
  run_simulation(g_state, g_state.controls.num_iters);
  MorphNodes node_vecs = read_nodes_from_vbos(g_state.morph_state);
  update_ui_for_output(g_state, node_vecs);
  gen_render_data(g_state, node_vecs);
  log_gl_errors("ending sim pipeline\n");
}

/*
void set_sample_render_data(RenderState& r_state) {
  vector<Vertex> vertices = {
    {vec3(0.0), vec3(0.0,0.0,-1.0), vec4(1.0,0.0,0.0,1.0), vec4(0.0)},
    {vec3(1.0,0.0,0.0), vec3(0.0,0.0,-1.0), vec4(1.0,0.0,0.0,1.0), vec4(0.0)},
    {vec3(1.0,1.0,0.0), vec3(0.0,0.0,-1.0), vec4(1.0,0.0,0.0,1.0), vec4(0.0)}
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
*/

void render_frame(GraphicsState& g_state) {
  RenderState& r_state = g_state.render_state;
  glUseProgram(r_state.prog.gl_handle);

  // set uniforms
  float aspect_ratio = r_state.fb_width / (float) r_state.fb_height;
  Camera& cam = g_state.camera;
  mat4 mv_matrix = glm::lookAt(cam.pos(), cam.pos() + cam.forward(), cam.up());
  mat4 proj_matrix = glm::perspective((float) M_PI / 4.0f, aspect_ratio, 1.0f, 10000.0f);
  glUniformMatrix4fv(r_state.prog.unif_mv_matrix, 1, 0, &mv_matrix[0][0]);
  glUniformMatrix4fv(r_state.prog.unif_proj_matrix, 1, 0, &proj_matrix[0][0]);

  // Set user uniform values
  for (UserUnif& user_unif : r_state.prog.user_unifs) {
    glUniform4fv(user_unif.gl_handle, 1, &user_unif.cur_val[0]);
  }

  //set_sample_render_data(r_state);

  glBindVertexArray(r_state.vao);
  glPointSize(10.0f);

  if (g_state.controls.render_faces && r_state.elem_count > 0) {
    glUniform1i(r_state.prog.unif_debug_render, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
    glDrawElements(GL_TRIANGLES, r_state.elem_count, GL_UNSIGNED_INT, nullptr);
  }
  vec3 debug_col(1.0,0.0,0.0);
  if (g_state.controls.render_wireframe && r_state.elem_count > 0) {
    glUniform1i(r_state.prog.unif_debug_render, 1);
    glUniform3fv(r_state.prog.unif_debug_color, 1, &debug_col[0]);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_state.index_buffer);
    glDrawElements(GL_TRIANGLES, r_state.elem_count, GL_UNSIGNED_INT, nullptr);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  if (g_state.controls.render_points && r_state.vertex_count > 0) {
    glUniform1i(r_state.prog.unif_debug_render, 1);
    glUniform3fv(r_state.prog.unif_debug_color, 1, &debug_col[0]);
    glDrawArrays(GL_POINTS, 0, r_state.vertex_count);
  }

  log_gl_errors("done render_frame");
}

void handle_key_event(GLFWwindow* win, int key, int scancode,
    int action, int mods) {
  GraphicsState* g_state = static_cast<GraphicsState*>(glfwGetWindowUserPointer(win));
  Controls& controls = g_state->controls;

  if (key == GLFW_KEY_R && action == GLFW_PRESS) {
    // reset camera pos
    g_state->camera = Camera();
  }
  if (key == GLFW_KEY_F && action == GLFW_PRESS) {
    controls.cam_spherical_mode = !controls.cam_spherical_mode;
  }
  if (key == GLFW_KEY_T && action == GLFW_PRESS) {
    g_state->camera = controls.zoom_to_fit_cam;
  }
  if (key == GLFW_KEY_P && action == GLFW_PRESS) {
    printf("reloading program\n");
    reload_current_programs(*g_state);
  }
  if (key == GLFW_KEY_C && action == GLFW_PRESS) {
    run_simulation_pipeline(*g_state);  
  }
}

void update_camera_cartesian(GLFWwindow* win, Controls& controls, Camera& cam) {
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
  vec3 trans = delta * 20.0f * 0.017f;
  mat4 trans_mat = glm::translate(mat4(1.0), trans);

  mat4 rot_mat(1.0);
  float amt_degs = 0.017f * 5.0f;
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

void update_camera_spherical(GLFWwindow* win, Controls& controls, Camera& cam) {
  vec3 eye = cam.pos();
  float cur_r = length(eye);
  float h_angle = atan2(eye.z, eye.x);
  float v_angle = atan2(length(vec2(eye.x, eye.z)), eye.y);

  float delta_r = 0.0;
  float delta_h_angle = 0.0;
  float delta_v_angle = 0.0;
  if (glfwGetKey(win, GLFW_KEY_W)) {
    delta_r = -1.0;
  }
  if (glfwGetKey(win, GLFW_KEY_A)) {
    delta_h_angle = 1.0;
  }
  if (glfwGetKey(win, GLFW_KEY_S)) {
    delta_r = 1.0;
  }
  if (glfwGetKey(win, GLFW_KEY_D)) {
    delta_h_angle = -1.0;
  }
  if (glfwGetKey(win, GLFW_KEY_Q)) {
    delta_v_angle = 1.0;
  }
  if (glfwGetKey(win, GLFW_KEY_E)) {
    delta_v_angle = -1.0;
  }
  float new_r = cur_r + 30.0f * delta_r * 0.017;
  float new_h_angle = h_angle + M_PI / 2.0 * delta_h_angle * 0.017;
  float new_v_angle = v_angle + M_PI / 2.0 * delta_v_angle * 0.017;

  vec3 pos = new_r * vec3(
      cos(new_h_angle) * sin(new_v_angle),
      cos(new_v_angle),
      sin(new_h_angle) * sin(new_v_angle)
      );
  cam.set_view(pos, vec3(0.0));
}

void update_camera(GLFWwindow* win, Controls& controls, Camera& cam) {
  if (controls.cam_spherical_mode) {
    update_camera_spherical(win, controls, cam);
  } else {
    update_camera_cartesian(win, controls, cam);
  }
  // if holding space, auto-adjust camera as the simulation progresses
  if (glfwGetKey(win, GLFW_KEY_SPACE)) {
    cam = controls.zoom_to_fit_cam;
  }
}

void gen_user_uniforms_ui(vector<UserUnif>& user_unifs) {
  bool should_reset_unifs = ImGui::Button("restore defaults");
  for (UserUnif& user_unif : user_unifs) {
    if (should_reset_unifs) {
      user_unif.cur_val = user_unif.def_val;
    }
    ImGui::DragScalarN(user_unif.name.c_str(), ImGuiDataType_Float,
        &user_unif.cur_val[0], user_unif.num_comps, user_unif.drag_speed,
        &user_unif.min, &user_unif.max, "%.3f");
  }
}

void run_app(int argc, char** argv) {
  signal(SIGSEGV, handle_segfault);

  // read cmd-line args
  if (argc < 2) {
    printf("Incorrect usage. Please use:\n\nexec path\n\n"
        "where \"path\" is the path to the directory containing the"
        " \"shaders\" folder. Ex: \"exec ..\"\n");
    return;
  }
  string base_shader_path(argv[1]);

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

  GraphicsState g_state(window, base_shader_path);
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
    update_camera(window, g_state.controls, g_state.camera);

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
    ImGui::Text("camera:");
    ImGui::Text("eye: %s", vec3_str(g_state.camera.pos()).c_str());
    ImGui::Text("forward: %s", vec3_str(g_state.camera.forward()).c_str());
    ImGui::Text("mode: %s", controls.cam_spherical_mode ? "spherical" : "cartesian");

    ImGui::Separator();
    ImGui::Text("render controls:");
    ImGui::Checkbox("render faces", &controls.render_faces);
    ImGui::Checkbox("render points", &controls.render_points);
    ImGui::Checkbox("render wireframe", &controls.render_wireframe);

    ImGui::Separator();
    ImGui::Text("render program controls:");
    ImGui::PushID("render");
    gen_user_uniforms_ui(g_state.render_state.prog.user_unifs);
    ImGui::PopID();

    ImGui::Separator();
    ImGui::Text("morph program controls:");
    MorphState& m_state = g_state.morph_state;

    vector<const char*> prog_names;
    for (auto& prog : g_state.morph_state.programs) {
      prog_names.push_back(prog.name.c_str());
    }
    ImGui::Combo("program", &m_state.cur_prog_index,
        prog_names.data(), prog_names.size());
      //ImGui::DragFloat4(user_unif.name.c_str(), &user_unif.cur_val[0]);

    MorphProgram& cur_prog = m_state.programs[m_state.cur_prog_index];
    ImGui::PushID("morph");
    gen_user_uniforms_ui(cur_prog.user_unifs);
    ImGui::PopID();
    
    ImGui::Separator();
    ImGui::Text("simulation controls:");
    ImGui::Text("seed:");
    ImGui::InputInt("AxA samples", &controls.num_zygote_samples);
    controls.num_zygote_samples = std::max(controls.num_zygote_samples, 0);
    
    ImGui::Text("simulation:"); 
    int max_iter_num = 1*1000*1000*1000;
    ImGui::DragInt("iter num", &controls.num_iters, 0.2f, 0, max_iter_num);
    if (ImGui::Button("run once")) {
      run_simulation_pipeline(g_state);  
    }
    ImGui::Text("animation:");
    string anim_btn_text(controls.animating_sim ? "PAUSE" : "PLAY");
    if (ImGui::Button(anim_btn_text.c_str())) {
      controls.animating_sim = !controls.animating_sim;
    }
    ImGui::DragInt("start iter", &controls.start_iter_num, 10.0f, 0, max_iter_num);
    ImGui::DragInt("end iter", &controls.end_iter_num, 10.0f, controls.start_iter_num, max_iter_num);
    ImGui::DragInt("delta iters per frame", &controls.delta_iters, 0.2f, -10, 10);
    ImGui::Checkbox("loop at end", &controls.loop_at_end);
    
    // run the animation
    if (controls.animating_sim) {
      controls.num_iters += controls.delta_iters;
      controls.num_iters = clamp(controls.num_iters,
          controls.start_iter_num, controls.end_iter_num);
      if (controls.loop_at_end && controls.num_iters == controls.end_iter_num) {
        controls.num_iters = controls.start_iter_num;
      }
      run_simulation_pipeline(g_state);
    }

    ImGui::Separator();
    ImGui::Text("debug");
    ImGui::Text("Note that logging will not occur while animating");
    ImGui::Checkbox("log input nodes", &controls.log_input_nodes);
    ImGui::Checkbox("log output nodes", &controls.log_output_nodes);
    ImGui::Checkbox("log render data", &controls.log_render_data);
    // do not log while animating, the IO becomes a bottleneck
    if (controls.animating_sim) {
      controls.log_input_nodes = false;
      controls.log_output_nodes = false;
      controls.log_render_data = false;
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

    g_state.render_state.fb_width = fb_width;
    g_state.render_state.fb_height = fb_height;
    render_frame(g_state);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
}
