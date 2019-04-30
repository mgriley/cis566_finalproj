#include "utils.h"

const char* INSTRUCTIONS_STRING = R"--(
quickstart:
Drag the "iter num" slider to see the mesh update in real-time.

camera:
WASDEQ to move
F to switch between cartesian and spherical movement
R to reset to original position

general:
P: reload programs (check stdout for errors)
C: run simulation once (can also be done via the button)

render program controls:
render_mode:
0: boring solid color
1: color by heat
2: color by heat generation amt
3: highlight source nodes

morph program controls:
You'll need to consult shaders/growth.glsl for the particular usage
of each uniform. The names are generally sensible.

simulation pane:
Running once runs the simulation for the desired number of frames, and
the result is rendered. The initial data is a grid of AxA vertices. Many
of the morph program tunable parameters default to values that work well
with about 100x100.

animation pane:
The app starts with the animation playing, but with a speed of 0. This regenerates
the mesh every frame but does not change the iteration num automatically.
To run the simulation forwards or backwards, change the "delta iters per frame" to +/-1.
While animating, the app runs "iter num" iterations every frame, so it may be slow.

)--";

void handle_segfault(int sig_num) {
  array<void*, 15> frames{};
  int num_frames = backtrace(frames.data(), frames.size());
  fprintf(stdout, "SEGFAULT to stdout");
  fprintf(stderr, "SEGFAULT signal: %d\n", sig_num);
  backtrace_symbols_fd(frames.data(), num_frames, STDERR_FILENO);
  exit(1);
}

string vec3_str(vec3 v) {
  array<char, 100> s;
  sprintf(s.data(), "[%5.2f, %5.2f, %5.2f]", v[0], v[1], v[2]);
  return string(s.data());
}

string vec4_str(vec4 v) {
  array<char, 100> s;
  sprintf(s.data(), "[%5.2f, %5.2f, %5.2f, %5.2f]", v[0], v[1], v[2], v[3]);
  return string(s.data());
}

string ivec4_str(ivec4 v) {
  array<char, 100> s;
  sprintf(s.data(), "[%4d, %4d, %4d, %4d]", v[0], v[1], v[2], v[3]); 
  return string(s.data());
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
  glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &value);
  printf("GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS %d\n", value);
  glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS, &value);
  printf("GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS %d\n", value);
  glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS, &value);
  printf("GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS %d\n", value);
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
