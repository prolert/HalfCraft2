#pragma once

// the tree of a map's .bsp (models[0].headnode, which server.dll reads when the map loads), in source units: which leaf
// a point is in, and so whether it is solid. pure code, no engine and no link.
//
// a node's children are node indices, or, when below 0, leaves: leaf -1 - child. BSP_CONTENTS_SOLID is the bsp's
// CONTENTS_SOLID.
//
// the walk is vbsp's PointLeafnum_r (src/utils/vbsp/writebsp.cpp:1370): in front of a node's plane where n.p - dist >= 0.
// a tree that is empty or damaged (an index out of range, a loop) reads as contents 0: a bad file mustn't hold a worker
// thread.

#include <array>
#include <cstddef>
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
}
