fix_positions comps 1 min 0.0 max 1.0 speed 1.0 default 0.0
norm_src_pos comps 1 min 0.0 max 1.0 speed 0.001 default 0.5
init_src_dir comps 3 min -1.0 max 1.0 speed 0.001 default 0.0 1.0 0.0
src_heat_gen_rate comps 1 min 0.0 max 100.0 speed 0.01 default 10.0
heat_transfer_coeff comps 1 min 0.0 max 1.0 speed 0.001 default 0.2
target_spring_len comps 1 min 0.0 max 3.0 speed 0.01 default 0.1
spring_coeffs comps 2 min 0.0 max 1.0 speed 0.01 default 0.05 0.1
force_coeffs comps 3 min 0.0 max 3.0 speed 0.01 default 0.0 0.25 0.25
src_trans_probs comps 3 min 0.0 max 1.0 speed 0.001 default 0.05 0.45 0.0
cloning_coeffs comps 3 min 0.0 max 1.0 speed 0.005 default 1.0 1.0 0.5
cloning_interval comps 1 min 0.0 max 600.0 speed 0.1 default 40.0
END_USER_UNIFS

#version 410

uniform int iter_num;
uniform int num_nodes;

// user-tunable uniforms
uniform vec4 norm_src_pos;
uniform vec4 init_src_dir;
uniform vec4 fix_positions;
uniform vec4 src_heat_gen_rate;
uniform vec4 heat_transfer_coeff;
uniform vec4 target_spring_len;
uniform vec4 spring_coeffs;
uniform vec4 force_coeffs;
uniform vec4 src_trans_probs;
uniform vec4 cloning_coeffs;
uniform vec4 cloning_interval;

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

const float pi = 3.141592;

// Noise functions

vec2 hash2(vec2 p) { p=vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))); return fract(sin(p)*18.5453); }
vec3 hash3(float n) { return fract(sin(vec3(n,n+1.0,n+2.0))*vec3(338.5453123,278.1459123,191.1234)); }
float hash(vec2 p) {
	return fract(dot(hash2(p),vec2(1.0,0.0)));
}
vec3 hash3(vec3 p) {
	p=vec3(dot(p,vec3(127.1,311.7,732.1)),dot(p,vec3(269.5,183.3,23.1)),dot(p,vec3(893.1,21.4,781.2))); return fract(sin(p)*18.5453);	
}
float hash3to1(vec3 p) {
	return fract(dot(hash3(p),vec3(32.32,321.3,123.2)));
}

void run_init_iter_test2() {
  int side_len = int(sqrt(num_nodes));
  int target_src_id = int(num_nodes * norm_src_pos.x + 0.25 * side_len);
  int target_src_id_b = int(num_nodes * norm_src_pos.x + 0.75 * side_len);
  if (gl_VertexID == target_src_id) {
    out_vel = vec4(vec3(0.0,1.0,0.0), src_heat_gen_rate.x);
  } else if(gl_VertexID == target_src_id_b) {
    out_vel = vec4(vec3(0.0,-1.0,0.0), src_heat_gen_rate.x);
  } else {
    out_vel = vec4(0.0);
  }
  out_pos = vec4(pos.xyz, 0.0);
  out_neighbors = neighbors;
  out_data = vec4(-1.0);
}

void run_init_iter() {
  int side_len = int(sqrt(num_nodes));
  int target_src_id = int(num_nodes * norm_src_pos.x + 0.5 * side_len);
  if (gl_VertexID == target_src_id) {
    out_vel = vec4(normalize(init_src_dir.xyz), src_heat_gen_rate.x);
  } else {
    out_vel = vec4(0.0);
  }
  out_pos = vec4(pos.xyz, 0.0);
  out_neighbors = neighbors;
  out_data = vec4(-1.0);
}

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

// TODO - unused
// Not actually the gradient, but same idea 
vec3 heat_gradient() {
  vec3 avg_delta_heat = vec3(0.0);
  for (int i = 0; i < 4; ++i) {
    int n_index = int(neighbors[i]);
    if (n_index != -1) {
      vec4 n_pos = texelFetch(pos_buf, n_index);
      avg_delta_heat += normalize(n_pos.xyz - pos.xyz) * (n_pos.w - pos.w);
    }
  }
  avg_delta_heat *= 0.25;
  return avg_delta_heat;
}

vec3 node_normal(vec3 node_pos, vec4 node_neighbors) {
  vec3 nor = vec3(0.0,1.0,0.0);
  for (int i = 0; i < 4; ++i) {
    int i_a = int(node_neighbors[i]);
    int i_b = int(node_neighbors[(i + 1) % 4]);
    if (i_a != -1 && i_b != -1) {
      vec3 p_a = texelFetch(pos_buf, i_a).xyz;
      vec3 p_b = texelFetch(pos_buf, i_b).xyz;
      // TODO - why is this the 'up' direction, seems like the negative
      // sign should be unneccessary
      nor = normalize(-cross(p_a - node_pos, p_b - node_pos));
      break;
    }
  }
  return nor;
}

int rand_neighbor_index(vec3 node_pos, vec4 node_neighbors) {
  int n_index = clamp(int(4.0 * hash3(iter_num * node_pos).x), 0, 3);
  // incr n_index until we find a valid neighbor
  for (int i = 0; i < 4; ++i) {
    if (node_neighbors[n_index] == -1.0) {
      n_index = (n_index + 1) % 4;
    }
  }
  return n_index;
}

// Return the index of the neighbor that is furthest in the target direction
// Note: target_dir must be normalized
int directed_neighbor(vec3 node_pos, vec4 node_neighbors, vec3 target_dir) {
  int out_index = -1;
  float largest_dot = -2.0;
  for (int i = 0; i < 4; ++i) {
    int n_index = int(node_neighbors[i]);
    if (n_index != -1) {
      vec3 n_pos = texelFetch(pos_buf, n_index).xyz;
      float d = dot(n_pos - node_pos, target_dir);
      if (out_index == -1 || d > largest_dot) {
        out_index = i;
        largest_dot = d;
      }
    }
  }
  return out_index;
}

void compute_source_transition(out vec4 out_vel, out vec4 out_data) {
  vec4 next_vel = vel;
  vec4 next_data = vec4(-1.0);

  vec3 trans_noise = hash3(pos.xyz * iter_num);
  if (vel.w == 0.0) {
    // check if a neighbor has requested to be cloned
    bool did_promote = false;
    for (int i = 0; i < 4; ++i) {
      int n_index = int(neighbors[i]);
      if (n_index != -1) {
        vec4 n_data = texelFetch(data_buf, n_index);
        if (int(n_data.w) == (i + 2) % 4) {
          // neighbor has requested that this node be its clone
          float gen_amt = length(n_data.xyz);
          next_vel = vec4(normalize(n_data.xyz), gen_amt);
          next_data = vec4(-1.0);
          did_promote = true;
        }
      }
    }

    // promote this node to a src with some probability
    if (!did_promote && trans_noise.z < src_trans_probs.z) {
      vec3 nor = node_normal(pos.xyz, neighbors);
      next_vel = vec4(nor, src_heat_gen_rate.x);
      next_data = vec4(-1.0);
    }
  } else {
    // this node is currently a source

    // clone if right conditions
    bool is_cloning = false;
    // TODO
    if (iter_num % int(cloning_interval.x) == 0) {
    //if (trans_noise.x < src_trans_probs.x) {
      // turn on the request
      // Note that we encode the gen_amt as the vector len.
      // This only works b/c the gen_amt is strictly positive!
      
      // create two new vecs mirrored across the current vec
      // pi * cloning_coeffs.z is the desired angle b/w the two vecs
      float tangent_len = tan(0.5 * pi * cloning_coeffs.z);
      vec3 tangent_vec = tangent_len * normalize(cross(vel.xyz, trans_noise));
      vec3 my_dir = normalize(vel.xyz - tangent_vec);
      vec3 clone_dir = normalize(vel.xyz + tangent_vec);

      float clone_gen_amt = cloning_coeffs.y * vel.w;
      //int target_n = rand_neighbor_index(pos.xyz, neighbors);
      int target_n = directed_neighbor(pos.xyz, neighbors, clone_dir);
      next_vel = vec4(my_dir, cloning_coeffs.x * vel.w);
      next_data = vec4(clone_gen_amt * clone_dir, target_n);
      is_cloning = true;
    }

    // traverse mesh if right conditions
    bool is_walking = false;
    if (!is_cloning && trans_noise.y < src_trans_probs.y) {
      // Note that neighbor could be a src, in which case
      // we effectively eliminate a src.
      //int target_n = rand_neighbor_index(pos.xyz, neighbors);
      int target_n = directed_neighbor(pos.xyz, neighbors, vel.xyz);
      next_vel = vec4(0.0);
      next_data = vec4(vel.w * vel.xyz, target_n);
      is_walking = true;
    }

    if (!is_cloning && !is_walking) {
      // no msg for neighbors
      next_vel = vel;
      next_data = vec4(-1.0);
    }
  }
  out_vel = next_vel;
  out_data = next_data;
}

vec3 compute_next_pos() {

  bool is_fixed = false;
  vec3 force = vec3(0.0);
  vec3 delta_heat = vec3(0.0);
  float largest_delta = 0.0;
  for (int i = 0; i < 4; ++i) {
    int n_i = int(neighbors[i]);
    if (n_i != -1) {
      vec4 n_pos = texelFetch(pos_buf, n_i);
      vec3 delta_pos = n_pos.xyz - pos.xyz;
      float spring_len = length(delta_pos);
      float spring_factor = spring_len < target_spring_len.x ?
        spring_coeffs.x : spring_coeffs.y;
      force += spring_factor * normalize(delta_pos) *
        (spring_len - target_spring_len.x);

      if (n_pos.w - pos.w > largest_delta) {
        delta_heat = normalize(n_pos.xyz - pos.xyz);      
        largest_delta = n_pos.w - pos.w;
      }
    } else {
      is_fixed = true;
    }
  }

  if (vel.w != 0.0) {
    force += force_coeffs.y * vel.xyz;
  } else {
    force += force_coeffs.z * delta_heat;
  }

  vec3 p_next = pos.xyz + force; 

  if (is_fixed) {
    p_next = pos.xyz;
  }
  return p_next;
}

void run_reg_iter() {
  vec3 next_pos = compute_next_pos();
  float next_heat = compute_next_heat();
  vec4 next_vel = vec4(0.0);
  vec4 next_data = vec4(0.0);
  compute_source_transition(next_vel, next_data);

  if (int(fix_positions.x) == 1.0) {
    next_pos = pos.xyz;
  }

  out_pos = vec4(next_pos, next_heat);
  out_vel = next_vel;
  out_neighbors = neighbors;
  out_data = next_data;
}

void main() {
  if (iter_num == 0) {
    run_init_iter();
  } else {
    run_reg_iter();
  }
}

/*
vec3 compute_next_pos_old() {

  bool is_fixed = false;
  vec3 force = vec3(0.0);
  vec3 avg_delta_heat = vec3(0.0);
  for (int i = 0; i < 4; ++i) {
    int n_i = int(neighbors[i]);
    if (n_i != -1) {
      vec4 n_pos = texelFetch(pos_buf, n_i);
      vec3 delta = n_pos.xyz - pos.xyz;
      float spring_len = length(delta);
      force += force_coeffs.x * normalize(delta) *
        (spring_len - target_spring_len.x);
      avg_delta_heat += normalize(n_pos.xyz - pos.xyz) * (n_pos.w - pos.w);
    } else {
      is_fixed = true;
    }
  }
  avg_delta_heat *= 0.25;

  if (vel.w != 0.0) {
    force += force_coeffs.y * vel.xyz;
  } else {
    force += force_coeffs.z * avg_delta_heat;
  }

  vec3 p_next = pos.xyz + force; 

  if (is_fixed) {
    p_next = pos.xyz;
  }
  return p_next;
}
*/
