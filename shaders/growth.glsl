foo comps 1 min 0.0 max 1.0 speed 0.01 default 1.0
bar comps 1 min 0.0 max 1.0 speed 0.01 default 1.0
norm_src_pos comps 1 min 0.0 max 1.0 speed 0.001 default 0.5
fix_positions comps 1 min 0.0 max 1.0 speed 1.0 default 0.0
src_heat_gen_rate comps 1 min 0.0 max 10.0 speed 0.01 default 4.0
heat_transfer_coeff comps 1 min 0.0 max 1.0 speed 0.001 default 0.2
END_USER_UNIFS

#version 410

uniform int iter_num;
uniform int num_nodes;

// user-tunable uniforms
uniform vec4 foo;
uniform vec4 norm_src_pos;
uniform vec4 fix_positions;
uniform vec4 src_heat_gen_rate;
uniform vec4 heat_transfer_coeff;

// use explicit locations so that these attributes in 
// different programs explicitly use the same attribute indices
layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 vel;
// right, up, left, down
layout (location = 2) in vec4 neighbors;
layout (location = 3) in vec4 data;

uniform samplerBuffer pos_buf;
uniform samplerBuffer vel_buf;
uniform samplerBuffer neighbors_buf;
uniform samplerBuffer data_buf;

out vec4 out_pos;
out vec4 out_vel;
out vec4 out_neighbors;
out vec4 out_data;

void run_init_iter() {
  int side_len = int(sqrt(num_nodes));
  int target_src_id = int(num_nodes * norm_src_pos.x + 0.5 * side_len);
  if (gl_VertexID == target_src_id) {
    float gen_rate = src_heat_gen_rate.x;
    vec3 force_dir = vec3(0.0,1.0,0.0);
    out_vel = vec4(vel.xyz, gen_rate);
    out_data = vec4(force_dir, 0.0);
  } else {
    out_vel = vec4(vel.xyz, 0.0);
    out_data = vec4(0.0);
  }
  out_pos = vec4(pos.xyz, 0.0);
  out_neighbors = neighbors;
}

// conserves heat, but allows negative heats, hard to reason about
/*
float compute_next_heat_naive() {
  float delta_heat = 0.0;
  for (int i = 0; i < 4; ++i) {
    int n_index = int(neighbors[i]);
    if (n_index == -1.0) {
      delta_heat -= 10.0f;
    } else {
      float n_heat = texelFetch(pos_buf, n_index).w;
      float heat_transfer = 0.5f * (n_heat - pos.w);
      delta_heat += heat_transfer;
    }
  }
  delta_heat += vel.w;
  return pos.w + delta_heat;
}
*/

vec4 compute_heat_emit(float cur_heat, vec4 node_neighbors) { 
  float alpha = heat_transfer_coeff.x;
  vec4 out_heats = vec4(0.0);
  for (int i = 0; i < 4; ++i) {
    int n_index = int(node_neighbors[i]);
    if (n_index == -1.0) {
      // treat exterior as 0-heat neighbor
      out_heats[i] = alpha * cur_heat;
    } else {
      float n_heat = texelFetch(pos_buf, n_index).w;
      if (n_heat < cur_heat) {
        out_heats[i] = alpha * (cur_heat - n_heat);
      }
    }
  }
  // normalize the amt emit out each edge so that we don't emit
  // more than we have available
  float total_emit = dot(out_heats, vec4(1.0));
  if (total_emit > 0.0) {
    out_heats = (out_heats / total_emit) * min(total_emit, cur_heat);
  }
  return out_heats;  
}

// Conserves heat and enforces a min heat of 0
// Note: this strategy works so long as compute_heat_emit conserves
// heat, which is nice.
// Mental model: at every frame transition you send out heat to neighbors
// and simultaneously receive heat from them.
float compute_next_heat() {
  vec4 my_out_heats = compute_heat_emit(pos.w, neighbors);
  float total_heat_out = dot(my_out_heats, vec4(1.0));

  float total_heat_in = 0.0;
  for (int i = 0; i < 4; ++i) {
    int n_index = int(neighbors[i]);
    if (n_index != -1.0) {
      float other_heat = texelFetch(pos_buf, n_index).w;
      if (pos.w < other_heat) {
        // the heat in from this neighbor is the heat that it emits along
        // the edge pointing to this node
        vec4 other_neighbors = texelFetch(neighbors_buf, n_index);
        vec4 other_out_heats = compute_heat_emit(other_heat, other_neighbors);
        // i is the index of edge a<->b according to a, and (i + 2) % 4 is
        // the index of edge a<->b according to b
        total_heat_in += other_out_heats[(i + 2) % 4];
      }
    }
  }
  total_heat_in += vel.w;
  return pos.w - total_heat_out + total_heat_in;
}

void run_reg_iter() {
  vec3 p0 = pos.xyz;
  vec3 v0 = vel.xyz;
  
  bool is_fixed = false;
  vec3 force = vec3(0.0);
  for (int i = 0; i < 4; ++i) {
    int n_i = int(neighbors[i]);
    if (n_i != -1) {
      vec3 n_pos = texelFetch(pos_buf, n_i).xyz;
      vec3 delta = n_pos - p0;
      float spring_len = length(delta);
      force += normalize(delta) * (spring_len - 0.5);
    } else {
      is_fixed = true;
    }
  }
  force += 1.0*vec3(0.0,-1.0,0.0);
  force += -0.5 * v0;

  if (is_fixed) {
    force = vec3(0.0);
  }

  // TODO
  float mass = foo.x;
  float delta_t = 0.017;
  vec3 accel = force / mass;
  vec3 v1 = v0 + accel*delta_t;
  vec3 p1 = p0 + v0 + accel*delta_t*delta_t/2.0;

  if (int(fix_positions.x) == 1.0) {
    p1 = p0;
  }

  float next_heat = compute_next_heat();

  out_pos = vec4(p1, next_heat);
  out_vel = vec4(v1, vel.w);
  out_neighbors = neighbors;
  out_data = data;
}

void main() {
  if (iter_num == 0) {
    run_init_iter();
  } else {
    run_reg_iter();
  }
}

