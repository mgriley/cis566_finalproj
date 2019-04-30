#pragma once

#include <vector>
#include <array>
#include <string>

#include <cstddef>
#include <stdlib.h>
#include <execinfo.h>
#include <stdio.h>
#include <unistd.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

using namespace std;
using namespace glm;

void handle_segfault(int sig_num);

string vec3_str(vec3 v);
string vec4_str(vec4 v);
string ivec4_str(ivec4 v);

void log_opengl_info();
void log_gl_errors(string msg);
void log_program_info_logs(string msg, GLuint program);
void log_shader_info_logs(string msg, GLuint shader);

extern const char* INSTRUCTIONS_STRING;
