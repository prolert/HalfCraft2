#pragma once

// the tree of a map's .bsp (models[0].headnode, which server.dll reads when the map loads), in source units: which leaf
// a point is in, and so whether it is solid. pure code, no engine and no link: read_bsp_tree takes the lumps' bytes.
//
// a node's children are node indices, or, when below 0, leaves: leaf -1 - child. BSP_CONTENTS_SOLID is the bsp's
// CONTENTS_SOLID.
//
// the walk is vbsp's PointLeafnum_r (src/utils/vbsp/writebsp.cpp:1370): in front of a node's plane where n.p - dist >= 0.
// a tree that is empty or damaged (an index out of range, a loop) reads as contents 0: a bad file mustn't hold a worker
// thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace halfcraft
{
	/// CONTENTS_SOLID: a brush's inside, and the leaves outside the map (vbsp's FillOutside)
	inline constexpr int BSP_CONTENTS_SOLID = 0x1;

	struct BspTree
	{
		std::vector<std::array<float, 4>> planes;         // {nx, ny, nz, dist}: in front where n.p - dist >= 0
		std::vector<std::array<int, 3>>   nodes;          // {plane, front child, back child}
		std::vector<int>                  leaf_contents;  // CONTENTS_* of each leaf
		int                               headnode = 0;   // where the walk starts (models[0].headnode)
	};

	/// @param p - a source position (units)
	/// @return the contents of the leaf p is in; 0 when the tree has none to give (it's empty or damaged)
	inline int point_contents(const BspTree& tree, const float p[3])
	{
		const std::size_t node_count = tree.nodes.size();
		int               child = tree.headnode;
		// a path through a sound tree takes each node once: more steps than nodes is a loop
		for (std::size_t step = 0; child >= 0 && step < node_count; ++step) {
			if (static_cast<std::size_t>(child) >= node_count) {
				return 0;
			}
			const std::array<int, 3>& node = tree.nodes[static_cast<std::size_t>(child)];
			if (node[0] < 0 || static_cast<std::size_t>(node[0]) >= tree.planes.size()) {
				return 0;
			}
			const std::array<float, 4>& plane = tree.planes[static_cast<std::size_t>(node[0])];
			const float                 d = plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] - plane[3];
			child = d >= 0.0f ? node[1] : node[2];
		}
		if (child >= 0) {
			return 0;
		}
		const std::size_t leaf = static_cast<std::size_t>(-1 - child);  // child is below 0: no overflow, no negative
		return leaf < tree.leaf_contents.size() ? tree.leaf_contents[leaf] : 0;
	}

	/// the .bsp's records the tree is read from (public/bspfile.h): dplane_t, dnode_t, and dleaf_t, whose version 0
	/// (dleaf_version_0_t) still carries the leaf's ambient light
	inline constexpr std::size_t BSP_PLANE_BYTES = 20, BSP_NODE_BYTES = 32, BSP_LEAF_BYTES_V0 = 56, BSP_LEAF_BYTES_V1 = 32;

	/// a map's tree from its .bsp's lumps. refused: a lump that isn't whole records, a leaf version but 0 and 1, no
	/// nodes or no leaves, and an index out of range, among them a child node that doesn't come after its node (vbsp
	/// writes a node before its children, so a walk down the tree ends).
	/// @param planes, nodes, leaves - the bytes of LUMP_PLANES, LUMP_NODES and LUMP_LEAFS
	/// @param leaf_version - LUMP_LEAFS's version
	/// @param headnode - models[0].headnode
	/// @param out - replaced: BspTree{} when refused
	/// @return whether the lumps make a tree
	inline bool read_bsp_tree(const std::vector<std::uint8_t>& planes, const std::vector<std::uint8_t>& nodes, const std::vector<std::uint8_t>& leaves,
		int leaf_version, int headnode, BspTree& out)
	{
		out = BspTree{};
		if (leaf_version != 0 && leaf_version != 1) {
			return false;
		}
		const std::size_t leaf_bytes = leaf_version == 0 ? BSP_LEAF_BYTES_V0 : BSP_LEAF_BYTES_V1;
		if (planes.size() % BSP_PLANE_BYTES != 0 || nodes.empty() || nodes.size() % BSP_NODE_BYTES != 0 || leaves.empty() || leaves.size() % leaf_bytes != 0) {
			return false;
		}
		const long long plane_count = static_cast<long long>(planes.size() / BSP_PLANE_BYTES);
		const long long node_count = static_cast<long long>(nodes.size() / BSP_NODE_BYTES);
		const long long leaf_count = static_cast<long long>(leaves.size() / leaf_bytes);
		if (headnode < 0 || headnode >= node_count) {
			return false;
		}

		BspTree tree;
		for (std::size_t at = 0; at < planes.size(); at += BSP_PLANE_BYTES) {
			std::array<float, 4> plane;  // dplane_t: normal, dist, then type
			std::memcpy(plane.data(), planes.data() + at, sizeof(plane));
			tree.planes.push_back(plane);
		}
		for (long long n = 0; n < node_count; ++n) {
			std::array<int, 3> node;  // dnode_t: planenum, children[2]
			std::memcpy(node.data(), nodes.data() + static_cast<std::size_t>(n) * BSP_NODE_BYTES, sizeof(node));
			if (node[0] < 0 || node[0] >= plane_count) {
				return false;
			}
			for (int k = 1; k < 3; ++k) {
				const long long child = node[k];
				if (child >= 0 ? child <= n || child >= node_count : -1 - child >= leaf_count) {
					return false;
				}
			}
			tree.nodes.push_back(node);
		}
		for (std::size_t at = 0; at < leaves.size(); at += leaf_bytes) {
			int contents;  // dleaf_t's first field
			std::memcpy(&contents, leaves.data() + at, sizeof(contents));
			tree.leaf_contents.push_back(contents);
		}
		tree.headnode = headnode;
		out = std::move(tree);
		return true;
	}
}
