/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup ply
 */
#include "BLI_array.hh"

#include "ply_data.hh"
#include "ply_file_buffer.hh"

namespace blender::io::ply {

void write_vertices(FileBuffer &buffer, const PlyData &ply_data)
{
  for (int i = 0; i < ply_data.vertices.size(); i++) {
    buffer.write_vertex(ply_data.vertices[i].x, ply_data.vertices[i].y, ply_data.vertices[i].z);

    if (!ply_data.vertex_normals.is_empty()) {
      buffer.write_vertex_normal(ply_data.vertex_normals[i].x,
                                 ply_data.vertex_normals[i].y,
                                 ply_data.vertex_normals[i].z);
    }

    if (!ply_data.vertex_colors.is_empty()) {
      buffer.write_vertex_color(uchar(ply_data.vertex_colors[i].x * 255),
                                uchar(ply_data.vertex_colors[i].y * 255),
                                uchar(ply_data.vertex_colors[i].z * 255),
                                uchar(ply_data.vertex_colors[i].w * 255));
    }

    if (!ply_data.uv_coordinates.is_empty()) {
      buffer.write_UV(ply_data.uv_coordinates[i].x, ply_data.uv_coordinates[i].y);
    }

    buffer.write_vertex_end();
  }
  buffer.write_to_file();
}

void write_faces(FileBuffer &buffer, const PlyData &ply_data)
{
  for (const Array<uint32_t> &face : ply_data.faces) {
    buffer.write_face(char(face.size()), face);
  }
  buffer.write_to_file();
}
void write_edges(FileBuffer &buffer, const PlyData &ply_data)
{
  for (const std::pair<int, int> &edge : ply_data.edges) {
    buffer.write_edge(edge.first, edge.second);
  }
  buffer.write_to_file();
}
}  // namespace blender::io::ply
