#include "types.h"

Camera::Camera()
{
  set_view(vec3(0.0,30.0,-30.0), vec3(0.0));
}

void Camera::set_view(vec3 eye, vec3 target) {
  vec3 forward = normalize(target - eye);
  vec3 right = normalize(cross(vec3(0.0,1.0,0.0), forward));
  vec3 up = cross(forward, right);
  
  cam_to_world[0] = vec4(right, 0.0);
  cam_to_world[1] = vec4(up, 0.0);
  cam_to_world[2] = vec4(forward, 0.0);
  cam_to_world[3] = vec4(eye, 1.0);
}

vec3 Camera::pos() const {
  return vec3(cam_to_world[3]);
}

vec3 Camera::right() const {
  return vec3(cam_to_world[0]);
}

vec3 Camera::up() const {
  return vec3(cam_to_world[1]);
}

vec3 Camera::forward() const {
  return vec3(cam_to_world[2]);
}

Vertex::Vertex(vec3 pos, vec3 nor, vec4 col, vec4 data) :
  pos(pos), nor(nor), col(col), data(data)
{
}

RenderProgram::RenderProgram() {
}

RenderProgram::RenderProgram(string name,
    vector<UserUnif>& user_unifs) :
  name(name), user_unifs(user_unifs)
{
}

RenderState::RenderState()
{
}

MorphNode::MorphNode():
  pos(0.0),
  vel(0.0),
  neighbors(-1.0),
  data(0.0)
{
}

MorphNode::MorphNode(vec4 pos, vec4 vel,
    vec4 neighbors, vec4 data) :
  pos(pos),
  vel(vel),
  neighbors(neighbors),
  data(data)
{
}

MorphNodes::MorphNodes(size_t num_nodes) :
  pos_vec(num_nodes),
  vel_vec(num_nodes),
  neighbors_vec(num_nodes),
  data_vec(num_nodes)
{
}

MorphNodes::MorphNodes(vector<MorphNode> const& nodes) :
  pos_vec(nodes.size()),
  vel_vec(nodes.size()),
  neighbors_vec(nodes.size()),
  data_vec(nodes.size())
{
  for (int i = 0; i < nodes.size(); ++i) {
    MorphNode const& node = nodes[i];
    pos_vec[i] = node.pos;
    vel_vec[i] = node.vel;
    neighbors_vec[i] = node.neighbors;
    data_vec[i] = node.data;
  }
}

MorphNode MorphNodes::node_at(size_t i) const {
  return MorphNode(pos_vec[i], vel_vec[i], 
      neighbors_vec[i], data_vec[i]);
}

string raw_node_str(MorphNode const& node) {
  array<char, 200> s;
  sprintf(s.data(), "pos: %s, vel: %s, neighbors: %s, data: %s",
        vec4_str(node.pos).c_str(),
        vec4_str(node.vel).c_str(),
        vec4_str(node.neighbors).c_str(),
        vec4_str(node.data).c_str());
  return string(s.data());
}

MorphBuffer::MorphBuffer() :
  vao(0)
{
}

UserUnif::UserUnif(string name, int num_comps, float min, float max,
    float drag_speed, vec4 def_val, vec4 cur_val) :
  name(name), gl_handle(-1), num_comps(num_comps), min(min), max(max),
  drag_speed(drag_speed), def_val(def_val), cur_val(cur_val)
{
}

MorphProgram::MorphProgram(string name, vector<UserUnif>& user_unifs) :
  name(name),
  gl_handle(-1),
  unif_iter_num(-1),
  user_unifs(user_unifs)
{
}

MorphState::MorphState() :
  result_buffer_index(0),
  cur_prog_index(0),
  num_nodes(0)
{
}

Controls::Controls() :
  render_faces(true),
  render_points(true),
  render_wireframe(true),
  log_input_nodes(false),
  log_output_nodes(false),
  log_render_data(false),
  num_zygote_samples(10),
  num_iters(0),
  cam_spherical_mode(true)
{
}

GraphicsState::GraphicsState(GLFWwindow* window,
    string base_shader_path) :
  window(window),
  base_shader_path(base_shader_path)
{
}

