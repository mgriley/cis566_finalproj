const char* TEST_VERTEX_SRC = R"--(
#version 410

void main() {
  gl_Position = vec4(0.0, 0.0, 0.5, 1.0);
}

)--";

const char* TEST_FRAGMENT_SRC = R"--(
#version 410

out vec4 color;

void main() {
  color = vec4(1.0,0.0,0.0,1.0);  
}

)--";
