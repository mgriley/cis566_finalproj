
// Modulo but wraps -ve values to the +ve range, giving a positive result
// Copied from: https://stackoverflow.com/questions/14997165/fastest-way-to-get-a-positive-modulo-in-c-c
inline int pos_mod(int i, int n) {
  return (i % n + n) % n;
}

// coord to vertex index
int coord_to_index_old(ivec2 coord, ivec2 samples) {
  return pos_mod(coord[0], samples[0]) + samples[0] * pos_mod(coord[1], samples[1]);
}


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

// a more compact node string
/*
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
*/

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


