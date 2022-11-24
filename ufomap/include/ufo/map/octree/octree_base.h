/*!
 * UFOMap: An Efficient Probabilistic 3D Mapping Framework That Embraces the Unknown
 *
 * @author Daniel Duberg (dduberg@kth.se)
 * @see https://github.com/UnknownFreeOccupied/ufomap
 * @version 1.0
 * @date 2022-05-13
 *
 * @copyright Copyright (c) 2022, Daniel Duberg, KTH Royal Institute of Technology
 *
 * BSD 3-Clause License
 *
 * Copyright (c) 2022, Daniel Duberg, KTH Royal Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *     list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UFO_MAP_OCTREE_BASE_H
#define UFO_MAP_OCTREE_BASE_H

// UFO
#include <ufo/algorithm/algorithm.h>
#include <ufo/map/code.h>
#include <ufo/map/io.h>
#include <ufo/map/key.h>
#include <ufo/map/node.h>
#include <ufo/map/octree/octree_iterator.h>
#include <ufo/map/octree/octree_node.h>
#include <ufo/map/octree/octree_predicate.h>
#include <ufo/map/point.h>
#include <ufo/map/predicate/predicates.h>
#include <ufo/map/predicate/spatial.h>
#include <ufo/map/types.h>
#include <ufo/math/util.h>
#include <ufo/util/iterator_wrapper.h>
#include <ufo/util/type_traits.h>

// STL
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ufo::map
{
// Utilizing curiously recurring template pattern (CRTP)
template <class Derived, class Data, class InnerData = void, bool ReuseNodes = false,
          UFOLock Lock = UFOLock::DEPTH, bool TrackNodes = false, bool CountNodes = true>
class OctreeBase
{
 private:
	using LeafNode = OctreeLeafNode<Data, ReuseNodes, TrackNodes>;
	using InnerNode = OctreeInnerNode<LeafNode, InnerData, UFOLock::NODE == Lock>;
	using InnerNodeBlock = typename InnerNode::InnerNodeBlock;
	using LeafNodeBlock = typename InnerNode::LeafNodeBlock;

	friend Derived;

 public:
	using const_iterator = IteratorWrapper<Derived, Node>;
	using const_query_iterator = const_iterator;
	using const_bounding_volume_iterator = IteratorWrapper<Derived, NodeBV>;
	using const_bounding_volume_query_iterator = const_bounding_volume_iterator;
	using const_query_nearest_iterator = IteratorWrapper<Derived, NearestNode>;

	using Query = util::IteratorWrapper<const_query_iterator>;
	using QueryBV = util::IteratorWrapper<const_bounding_volume_query_iterator>;
	using QueryNearest = util::IteratorWrapper<const_query_nearest_iterator>;

 public:
	/**************************************************************************************
	|                                                                                     |
	|                                       Octree                                        |
	|                                                                                     |
	**************************************************************************************/

	//
	// Clear
	//

	/*!
	 * @brief Erases the map. After this call, the map contains only the root node.
	 *
	 * @param prune Whether the memory should be cleared.
	 */
	void clear(bool prune = false) { clear(size(), depthLevels(), prune); }

	/*!
	 * @brief Erases the map and changes the leaf node size and the number of depth levels.
	 * After this call, the map contains only the root node.
	 *
	 * @param leaf_size The new leaf node size.
	 * @param depth_levels The new number of depth levels.
	 * @param prune Whether the memory should be cleared.
	 */
	void clear(node_size_t leaf_size, depth_t depth_levels, bool prune = false)
	{
		deleteChildren(root(), rootDepth(), prune);
		setNodeSizeAndDepthLevels(leaf_size, depth_levels);
		derived().initRoot();
	}

	//
	// Automatic pruning
	//

	// FIXME: Come up with better names

	/*!
	 * @brief Check if autonomaic pruning is enabled.
	 *
	 * @return Whether automatic pruning is enabled.
	 */
	[[nodiscard]] constexpr bool automaticPruning() const noexcept
	{
		return automatic_prune_;
	}

	/*!
	 * @brief Set (/turn on) automatic pruning.
	 */
	constexpr void setAutomaticPruning() noexcept { automatic_prune_ = true; }

	/*!
	 * @brief Reset (/turn off) automatic pruning.
	 */
	constexpr void resetAutomaticPruning() noexcept { automatic_prune_ = false; }

	//
	// Depth levels
	//

	/*!
	 * @brief The number of depth levels the octree currently have.
	 *
	 * @return The number of depth levels the octree currently have.
	 */
	[[nodiscard]] constexpr depth_t depthLevels() const noexcept { return depth_levels_; }

	/*!
	 * @brief The minimum depth levels an octree can have.
	 *
	 * @return The minimum depth levels an octree can have.
	 */
	[[nodiscard]] static constexpr depth_t minDepthLevels() { return 3; }

	/*!
	 * @brief The maximum depth levels an octree can have.
	 *
	 * @return The maximum depth levels an octree can have.
	 */
	[[nodiscard]] static constexpr depth_t maxDepthLevels() { return 22; }

	//
	// Size
	//

	/*!
	 * @brief Get the node size at a specific depth.
	 *
	 * @param depth The depth.
	 * @return The node size at the depth.
	 */
	[[nodiscard]] constexpr node_size_t size(depth_t depth = 0) const
	{
		return node_size_[depth];
	}

	//
	// Volume
	//

	/*!
	 * @brief The volume of the octree.
	 *
	 * @note Same as `size(rootDepth()) * size(rootDepth()) * size(rootDepth())`.
	 *
	 * @return The volume of the octree.
	 */
	[[nodiscard]] constexpr node_size_t volume() const
	{
		auto const s = size(rootDepth());
		return s * s * s;
	}

	//
	// Center
	//

	/*!
	 * @return The center of the octree.
	 */
	[[nodiscard]] Point center() const { return Point(0, 0, 0); }

	//
	// Bounding volume
	//

	/*!
	 * @return Minimum bounding volume convering the whole octree.
	 */
	[[nodiscard]] geometry::AAEBB boundingVolume() const
	{
		return geometry::AAEBB(center(), size(rootDepth() - 1));
	}

	//
	// Within
	//

	/*!
	 * @brief Check if a coordinate is within the octree bounds.
	 *
	 * @param coord The coordinate.
	 * @return Whether the coordinate is within the octree bounds.
	 */
	[[nodiscard]] constexpr bool isWithin(Point coord) const
	{
		return isWithin(coord.x, coord.y, coord.z);
	}

	/*!
	 * @brief Check if a coordinate is within the octree bounds.
	 *
	 * @param x,y,z The coordinate.
	 * @return Whether the coordinate is within the octree bounds.
	 */
	[[nodiscard]] constexpr bool isWithin(coord_t x, coord_t y, coord_t z) const
	{
		auto const max = size(rootDepth() - 1);
		auto const min = -max;
		return min <= x && min <= y && min <= z && max >= x && max >= y && max >= z;
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Leaf                                         |
	|                                                                                     |
	**************************************************************************************/

	//
	// Pure leaf
	//

	/*!
	 * @brief Check if a node is a pure leaf node (i.e., can never have children).
	 *
	 * @param node The node to check.
	 * @return Whether the node is a pure leaf node.
	 */
	[[nodiscard]] static constexpr bool isPureLeaf(Node node) noexcept
	{
		return 0 == node.depth();
	}

	/*!
	 * @brief Check if a node corresponding to a code is a pure leaf node (i.e., can never
	 * have children).
	 *
	 * @note Only have to check if the depth of the code is 0.
	 *
	 * @param code The code of the node to check.
	 * @return Whether the node is a pure leaf node.
	 */
	[[nodiscard]] static constexpr bool isPureLeaf(Code code) noexcept
	{
		return 0 == code.depth();
	}

	/*!
	 * @brief Check if a node corresponding to a key is a pure leaf node (i.e., can never
	 * have children).
	 *
	 * @note Only have to check if the depth of the key is 0.
	 *
	 * @param key The key of the node to check.
	 * @return Whether the node is a pure leaf node.
	 */
	[[nodiscard]] static constexpr bool isPureLeaf(Key key) noexcept
	{
		return 0 == key.depth();
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is a
	 * pure leaf node (i.e., can never have children).
	 *
	 * @note Only have to check if the depth is 0.
	 *
	 * @param coord The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is a pure leaf node.
	 */
	[[nodiscard]] static constexpr bool isPureLeaf(Point coord, depth_t depth = 0) noexcept
	{
		return 0 == depth;
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is a
	 * pure leaf node (i.e., can never have children).
	 *
	 * @note Only have to check if the depth is 0.
	 *
	 * @param x,y,z The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is a pure leaf node.
	 */
	[[nodiscard]] static constexpr bool isPureLeaf(coord_t x, coord_t y, coord_t z,
	                                               depth_t depth = 0) noexcept
	{
		return 0 == depth;
	}

	//
	// Leaf
	//

	/*!
	 * @brief Check if a node is a leaf node (i.e., has no children).
	 *
	 * @param node The node to check.
	 * @return Whether the node is a leaf node.
	 */
	[[nodiscard]] constexpr bool isLeaf(Node node) const
	{
		return isPureLeaf(node) || innerNode(node).leaf[node.index()];
	}

	/*!
	 * @brief Check if a node corresponding to a code is a leaf node (i.e., has no
	 * children).
	 *
	 * @param code The code of the node to check.
	 * @return Whether the node is a leaf node.
	 */
	[[nodiscard]] bool isLeaf(Code code) const
	{
		if (isPureLeaf(code)) {
			return true;
		}
		auto [node, depth] = innerNodeAndDepth(code);
		return node.leaf[code.index(depth)];
	}

	/*!
	 * @brief Check if a node corresponding to a key is a leaf node (i.e., has no children).
	 *
	 * @param key The key of the node to check.
	 * @return Whether the node is a leaf node.
	 */
	[[nodiscard]] bool isLeaf(Key key) const
	{
		return isPureLeaf(key) || isLeaf(toCode(key));
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is a leaf
	 * node (i.e., has no children).
	 *
	 * @param coord The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is a leaf node.
	 */
	[[nodiscard]] bool isLeaf(Point coord, depth_t depth = 0) const
	{
		return isPureLeaf(coord, depth) || isLeaf(toCode(coord, depth));
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is a leaf
	 * node (i.e., has no children).
	 *
	 * @param x,y,z The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is a leaf node.
	 */
	[[nodiscard]] bool isLeaf(coord_t x, coord_t y, coord_t z, depth_t depth = 0) const
	{
		return isPureLeaf(x, y, z, depth) || isLeaf(toCode(x, y, z, depth));
	}

	//
	// Parent
	//

	/*!
	 * @brief Check if a node is a parent (i.e., has children).
	 *
	 * @param node The node to check.
	 * @return Whether the node is a parent.
	 */
	[[nodiscard]] constexpr bool isParent(Node node) const { return !isLeaf(node); }

	/*!
	 * @brief Check if a node corresponding to a code is a parent (i.e., has
	 * children).
	 *
	 * @param code The code of the node to check.
	 * @return Whether the node is a parent.
	 */
	[[nodiscard]] constexpr bool isParent(Code code) const { return !isLeaf(code); }

	/*!
	 * @brief Check if a node corresponding to a key is a parent (i.e., has
	 * children).
	 *
	 * @param key The key of the node to check.
	 * @return Whether the node is a parent.
	 */
	[[nodiscard]] constexpr bool isParent(Key key) const { return !isLeaf(key); }

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is a parent
	 * (i.e., has children).
	 *
	 * @param coord The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is a parent.
	 */
	[[nodiscard]] constexpr bool isParent(Point coord, depth_t depth = 0) const
	{
		return !isLeaf(coord, depth);
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is a parent
	 * (i.e., has children).
	 *
	 * @param x,y,z The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is a parent.
	 */
	[[nodiscard]] constexpr bool isParent(coord_t x, coord_t y, coord_t z,
	                                      depth_t depth = 0) const
	{
		return !isLeaf(x, y, z, depth);
	}

	//
	// Exists
	//

	// TODO: Add comment
	[[nodiscard]] constexpr bool exists(Node node) const
	{
		if constexpr (ReuseNodes) {
		}

		if constexpr (TrackNodes) {
		}

		// FIXME: Optimize
		return operator()(node).isActualData();
	}

	[[nodiscard]] constexpr bool exists(Code code) const
	{
		return leafNodeAndDepth(code).second == code.depth();
	}

	[[nodiscard]] constexpr bool exists(Key key) const { return exists(toCode(key)); }

	[[nodiscard]] constexpr bool exists(Point coord, depth_t depth = 0) const
	{
		return exists(toCode(coord, depth));
	}

	[[nodiscard]] constexpr bool exists(coord_t x, coord_t y, coord_t z,
	                                    depth_t depth = 0) const
	{
		return exists(toCode(x, y, z, depth));
	}

	/**************************************************************************************
	|                                                                                     |
	|                                      Modified                                       |
	|                                                                                     |
	**************************************************************************************/

	//
	// Modified
	//

	/*!
	 * @brief Check if the octree is in a modified state (i.e., at least one node has been
	 * modified).
	 *
	 * @return Whether the octree is in a modified state.
	 */
	[[nodiscard]] constexpr bool isModified() const { return root().modified[rootIndex()]; }

	/*!
	 * @brief Check if a node of the octree is in a modified state (i.e., the node
	 * or one of its children has been modified).
	 *
	 * @param node The node to check.
	 * @return Whether the node is in a modified state.
	 */
	[[nodiscard]] constexpr bool isModified(Node node) const
	{
		return leafNode(node).modified[node.index()];
	}

	/*!
	 * @brief Check if a node corresponding to a code is in a modified state (i.e., the node
	 * or one of its children has been modified).
	 *
	 * @param code The code of the node to check.
	 * @return Whether the node is in a modified state.
	 */
	[[nodiscard]] constexpr bool isModified(Code code) const
	{
		auto [node, depth] = leafNodeAndDepth(code);
		return node.modified[code.index(depth)];
	}

	/*!
	 * @brief Check if a node corresponding to a key is in a modified state (i.e., the node
	 * or one of its children has been modified).
	 *
	 * @param key The key of the node to check.
	 * @return Whether the node is in a modified state.
	 */
	[[nodiscard]] constexpr bool isModified(Key key) const
	{
		return isModified(toCode(key));
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is in a
	 * modified state (i.e., the node or one of its children has been modified).
	 *
	 * @param coord The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is in a modified state.
	 */
	[[nodiscard]] constexpr bool isModified(Point coord, depth_t depth = 0) const
	{
		return isModified(toCode(coord, depth));
	}

	/*!
	 * @brief Check if a node corresponding to a coordinate at a specified depth is in a
	 * modified state (i.e., the node or one of its children has been modified).
	 *
	 * @param x,y,z The coordinate of the node to check.
	 * @param depth The depth of the node to check.
	 * @return Whether the node is in a modified state.
	 */
	[[nodiscard]] constexpr bool isModified(coord_t x, coord_t y, coord_t z,
	                                        depth_t depth = 0) const
	{
		return isModified(toCode(x, y, z, depth));
	}

	//
	// Set modified
	//

	/*!
	 * @brief Set all nodes down to and including 'min_depth' to modified state.
	 *
	 * @param min_depth The minimum depth to set nodes to the modified state.
	 */
	void setModified(depth_t min_depth = 0)
	{
		if (rootDepth() >= min_depth) {
			setModified(root(), rootIndex(), rootDepth(), min_depth);
		}
	}

	/*!
	 * @brief Set a node and all its children down to and including 'min_depth' to the
	 * modified state. This will also set the parents of the node to the modified state.
	 *
	 * @param node The node.
	 * @param min_depth The minimum depth to set nodes to the modified state.
	 */
	void setModified(Node node, depth_t min_depth = 0)
	{
		if (rootDepth() < min_depth) {
			return;
		}

		if (node.depth() < min_depth) {
			setModifiedParents(node.code().toDepth(min_depth - 1));
		} else {
			if (isPureLeaf(node)) {
				setModified(leafNode(node), node.index());
			} else {
				setModified(innerNode(node), node.index(), node.depth(), min_depth);
			}
			setModifiedParents(node.code().toDepth(min_depth));
		}
	}

	/*!
	 * @brief Set a node, corresponding to a code, and all its children down to and
	 * including 'min_depth' to the modified state. This will also set the parents of the
	 * node to the modified state.
	 *
	 * @param code The code of the node.
	 * @param min_depth The minimum depth to set nodes to the modified state.
	 */
	void setModified(Code code, depth_t min_depth = 0)
	{
		if (rootDepth() < min_depth) {
			return;
		}

		if (code.depth() < min_depth) {
			setModifiedParents(code.toDepth(min_depth - 1));
		} else {
			if (isPureLeaf(code)) {
				auto [node, depth] = leafNodeAndDepth(code);
				setModified(node, code.index(depth));
			} else {
				auto [node, depth] = innerNodeAndDepth(code);
				setModified(node, code.index(depth), depth, min_depth);
			}
			setModifiedParents(code.toDepth(min_depth));
		}
	}

	/*!
	 * @brief Set a node, corresponding to a key, and all its children down to and including
	 * 'min_depth' to the modified state. This will also set the parents of the node to the
	 * modified state.
	 *
	 * @param key The key of the node.
	 * @param min_depth The minimum depth to set nodes to the modified state.
	 */
	void setModified(Key key, depth_t min_depth = 0)
	{
		setModified(toCode(key), min_depth);
	}

	/*!
	 * @brief Set a node, corresponding to a coordinate and a specified depth, and all its
	 * children down to and including 'min_depth' to the modified state. This will also set
	 * the parents of the node to the modified state.
	 *
	 * @param coord The coordinate of the node.
	 * @param min_depth The minimum depth to set nodes to the modified state.
	 * @param depth The depth of the node.
	 */
	void setModified(Point coord, depth_t min_depth = 0, depth_t depth = 0)
	{
		setModified(toCode(coord, depth), min_depth);
	}

	/*!
	 * @brief Set a node, corresponding to a coordinate and a specified depth, and all its
	 * children down to and including 'min_depth' to the modified state. This will also set
	 * the parents of the node to the modified state.
	 *
	 * @param x,y,z The coordinate of the node.
	 * @param min_depth The minimum depth to set nodes to the modified state.
	 * @param depth The depth of the node.
	 */
	void setModified(coord_t x, coord_t y, coord_t z, depth_t min_depth = 0,
	                 depth_t depth = 0)
	{
		setModified(toCode(x, y, z, depth), min_depth);
	}

	//
	// Reset modified
	//

	/*!
	 * @brief Reset all nodes up to and including 'max_depth' from modified state.
	 *
	 * @note Only use this if you know what you are doing or if you do *not* plan to query
	 * for information in the octree. This function does not propagate information up the
	 * tree, hence queries will not function as expected. If you want to make queries then
	 * you should use 'propagateModified' instead.
	 *
	 * @param max_depth The maximum depth to reset nodes from the modified state.
	 */
	void resetModified(depth_t max_depth = maxDepthLevels())
	{
		resetModified(root(), rootIndex(), rootDepth(), max_depth);
	}

	/*!
	 * @brief Reset a node and its children up to and including 'max_depth' from modified
	 * state.
	 *
	 * @note Only use this if you know what you are doing or if you do *not* plan to query
	 * for information in the octree. This function does not propagate information up the
	 * tree, hence queries will not function as expected. If you want to make queries then
	 * you should use 'propagateModified' instead.
	 *
	 * @param node The node.
	 * @param max_depth The maximum depth to reset nodes from the modified state.
	 */
	void resetModified(Node node, depth_t max_depth = maxDepthLevels())
	{
		if (isPureLeaf(node)) {
			resetModified(leafNode(node), node.index());
		} else {
			resetModified(innerNode(node), node.index(), node.depth(), max_depth);
		}
	}

	/*!
	 * @brief Reset a node, corresponding to a code, and its children up to and including
	 * 'max_depth' from modified state.
	 *
	 * @note Only use this if you know what you are doing or if you do *not* plan to query
	 * for information in the octree. This function does not propagate information up the
	 * tree, hence queries will not function as expected. If you want to make queries then
	 * you should use 'propagateModified' instead.
	 *
	 * @param code The code of the node.
	 * @param max_depth The maximum depth to reset nodes from the modified state.
	 */
	void resetModified(Code code, depth_t max_depth = maxDepthLevels())
	{
		if (isPureLeaf(code)) {
			auto [node, depth] = leafNodeAndDepth(code);
			if (code.depth() == depth) {
				resetModified(node, code.index(depth));
			}
		} else {
			auto [node, depth] = innerNodeAndDepth(code);
			if (code.depth() == depth) {
				resetModified(node, code.index(depth), depth, max_depth);
			}
		}
	}

	/*!
	 * @brief Reset a node, corresponding to a key, and its children up to and including
	 * 'max_depth' from modified state.
	 *
	 * @note Only use this if you know what you are doing or if you do *not* plan to query
	 * for information in the octree. This function does not propagate information up the
	 * tree, hence queries will not function as expected. If you want to make queries then
	 * you should use 'propagateModified' instead.
	 *
	 * @param key The key of the node.
	 * @param max_depth The maximum depth to reset nodes from the modified state.
	 */
	void resetModified(Key key, depth_t max_depth = maxDepthLevels())
	{
		resetModified(toCode(key), max_depth);
	}

	/*!
	 * @brief Reset a node, corresponding to a coordinate and a specified depth, and its
	 * children up to and including 'max_depth' from modified state.
	 *
	 * @note Only use this if you know what you are doing or if you do *not* plan to query
	 * for information in the octree. This function does not propagate information up the
	 * tree, hence queries will not function as expected. If you want to make queries then
	 * you should use 'propagateModified' instead.
	 *
	 * @param coord The coordinate of the node.
	 * @param max_depth The maximum depth to reset nodes from the modified state.
	 * @param depth The depth of the node.
	 */
	void resetModified(Point coord, depth_t max_depth = maxDepthLevels(), depth_t depth = 0)
	{
		resetModified(toCode(coord, depth), max_depth);
	}

	/*!
	 * @brief Reset a node, corresponding to a coordinate and a specified depth, and its
	 * children up to and including 'max_depth' from modified state.
	 *
	 * @note Only use this if you know what you are doing or if you do *not* plan to query
	 * for information in the octree. This function does not propagate information up the
	 * tree, hence queries will not function as expected. If you want to make queries then
	 * you should use 'propagateModified' instead.
	 *
	 * @param x,y,z The coordinate of the node.
	 * @param max_depth The maximum depth to reset nodes from the modified state.
	 * @param depth The depth of the node.
	 */
	void resetModified(coord_t x, coord_t y, coord_t z,
	                   depth_t max_depth = maxDepthLevels(), depth_t depth = 0)
	{
		resetModified(toCode(x, y, z, depth), max_depth);
	}

	//
	// Propagate
	//

	/*!
	 * @brief Propagate modified information up the octree to and including 'max_depth'.
	 *
	 * @param keep_modified Whether propagated node's modified state should be reset.
	 * @param max_depth The maximum depth to propagate information.
	 */
	void propagateModified(bool keep_modified = false, depth_t max_depth = maxDepthLevels())
	{
		propagateModified(root(), rootIndex(), rootDepth(), keep_modified, max_depth);
	}

	/*!
	 * @brief Propagate a node's children modified information up to and including
	 * 'max_depth'.
	 *
	 * @param node The node.
	 * @param keep_modified Whether propagated node's modified state should be reset.
	 * @param max_depth The maximum depth to propagate information.
	 */
	void propagateModified(Node node, bool keep_modified = false,
	                       depth_t max_depth = maxDepthLevels())
	{
		if (isPureLeaf(node)) {
			propagateModified(leafNode(node), node.index(), keep_modified);
		} else {
			propagateModified(innerNode(node), node.index(), node.depth(), keep_modified,
			                  max_depth);
		}
	}

	/*!
	 * @brief Propagate a node's, corresponding to a code, children modified information up
	 * to and including 'max_depth'.
	 *
	 * @param code The code of the node.
	 * @param keep_modified Whether propagated node's modified state should be reset.
	 * @param max_depth The maximum depth to propagate information.
	 */
	void propagateModified(Code code, bool keep_modified = false,
	                       depth_t max_depth = maxDepthLevels())
	{
		if (isPureLeaf(code)) {
			auto [node, depth] = leafNodeAndDepth(code);
			if (code.depth() == depth) {
				propagateModified(node, code.index(depth), keep_modified);
			}
		} else {
			auto [node, depth] = innerNodeAndDepth(code);
			if (code.depth() == depth) {
				propagateModified(node, code.index(depth), depth, keep_modified, max_depth);
			}
		}
	}

	/*!
	 * @brief Propagate a node's, corresponding to a key, children modified information up
	 * to and including 'max_depth'.
	 *
	 * @param key The key of the node.
	 * @param keep_modified Whether propagated node's modified state should be reset.
	 * @param max_depth The maximum depth to propagate information.
	 */
	void propagateModified(Key key, bool keep_modified = false,
	                       depth_t max_depth = maxDepthLevels())
	{
		propagateModified(toCode(key), keep_modified, max_depth);
	}

	/*!
	 * @brief Propagate a node's, corresponding to a coordinate and a specified depth,
	 * children modified information up to and including 'max_depth'.
	 *
	 * @param coord The coordinate of the node.
	 * @param keep_modified Whether propagated node's modified state should be reset.
	 * @param max_depth The maximum depth to propagate information.
	 * @param depth The depth of the node.
	 */
	void propagateModified(Point coord, bool keep_modified = false,
	                       depth_t max_depth = maxDepthLevels(), depth_t depth = 0)
	{
		propagateModified(toCode(coord, depth), depth, keep_modified, max_depth);
	}

	/*!
	 * @brief Propagate a node's, corresponding to a coordinate and a specified depth,
	 * children modified information up to and including 'max_depth'.
	 *
	 * @param x,y,z The coordinate of the node.
	 * @param keep_modified Whether propagated node's modified state should be reset.
	 * @param max_depth The maximum depth to propagate information.
	 * @param depth The depth of the node.
	 */
	void propagateModified(coord_t x, coord_t y, coord_t z, bool keep_modified = false,
	                       depth_t max_depth = maxDepthLevels(), depth_t depth = 0)
	{
		propagateModified(toCode(x, y, z, depth), keep_modified, max_depth);
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Root                                         |
	|                                                                                     |
	**************************************************************************************/

	//
	// Is
	//

	/*!
	 * @brief Check if a node is the root node.
	 *
	 * @param node The node.
	 * @return Whether the node is the root node.
	 */
	[[nodiscard]] bool isRoot(Node node) const { return isRoot(node.code()); }

	/*!
	 * @brief Check if a node, corresponding to a code, is the root node.
	 *
	 * @param code The code of the node.
	 * @return Whether the node is the root node.
	 */
	[[nodiscard]] bool isRoot(Code code) const { return rootCode() == code; }

	/*!
	 * @brief Check if a node, corresponding to a key, is the root node.
	 *
	 * @param key The key of the node.
	 * @return Whether the node is the root node.
	 */
	[[nodiscard]] bool isRoot(Key key) const { return isRoot(toCode(key)); }

	/*!
	 * @brief Check if a node, corresponding to a coordinate at a specific depth, is the
	 * root node.
	 *
	 * @param coord The coordinate of the node.
	 * @param depth The depth of the node.
	 * @return Whether the node is the root node.
	 */
	[[nodiscard]] bool isRoot(Point coord, depth_t depth = 0) const
	{
		return isRoot(toCode(coord, depth));
	}

	/*!
	 * @brief Check if a node, corresponding to a coordinate at a specific depth, is the
	 * root node.
	 *
	 * @param x,y,z The coordinate of the node.
	 * @param depth The depth of the node.
	 * @return Whether the node is the root node.
	 */
	[[nodiscard]] bool isRoot(coord_t x, coord_t y, coord_t z, depth_t depth = 0) const
	{
		return isRoot(toCode(x, y, z, depth));
	}

	//
	// Get
	//

	/*!
	 * @brief Get the root node.
	 *
	 * @return The root node.
	 */
	[[nodiscard]] constexpr Node rootNode() const
	{
		return Node(const_cast<InnerNode*>(&root()), rootCode(), rootDepth());
	}

	/*!
	 * @brief Get the root node with bounding volume.
	 *
	 * @return The root node with bounding volume.
	 */
	[[nodiscard]] constexpr NodeBV rootNodeBV() const
	{
		return NodeBV(rootNode(), rootBoundingVolume());
	}

	//
	// Code
	//

	/*!
	 * @brief Get the code for the root node.
	 *
	 * @return The root node code.
	 */
	[[nodiscard]] constexpr Code rootCode() const
	{
		return Code(rootIndex() << 3 * rootDepth(), rootDepth());
	}

	//
	// Depth
	//

	/*!
	 * @brief Get the depth of the root node.
	 *
	 * @return The depth of the root node.
	 */
	[[nodiscard]] constexpr depth_t rootDepth() const noexcept { return depthLevels() - 1; }

	//
	// Size
	//

	/*!
	 * @brief Get the size of the root node.
	 *
	 * @note This is the same as `size()`.
	 *
	 * @return The size of the root node.
	 */
	[[nodiscard]] constexpr node_size_t rootSize() const noexcept { return size(); }

	//
	// Center
	//

	/*!
	 * @brief Get the center of the root node.
	 *
	 * @note This is the same as `center()`.
	 *
	 * @return The center of the root node.
	 */
	[[nodiscard]] constexpr Point rootCenter() const noexcept { return center(); }

	//
	// Bounding volume
	//

	/*!
	 * @brief Get the bounding volume of the root node.
	 *
	 * @note This is the same as `boundingVolmue()`.
	 *
	 * @return The bounding volume of the root node.
	 */
	[[nodiscard]] constexpr geometry::AAEBB rootBoundingVolume() const noexcept
	{
		return boundingVolume();
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Node                                         |
	|                                                                                     |
	**************************************************************************************/

	//
	// Size
	//

	/*!
	 * @brief Get the size of a node.
	 *
	 * @param node The node.
	 * @return The size of the node.
	 */
	[[nodiscard]] constexpr node_size_t size(Node node) const { return size(node.depth()); }

	//
	// Center
	//

	/*!
	 * @brief The center of a node.
	 *
	 * @param node The node.
	 * @return The center of the node.
	 */
	template <class NodeType>
	[[nodiscard]] Point center(NodeType const& node) const
	{
		if constexpr (std::is_same_v<NodeBV, NodeType>) {
			return node.center();
		} else {
			return toCoord(node.code());
		}
	}

	//
	// Bounding volume
	//

	/*!
	 * @brief Bounding volume for a node.
	 *
	 * @param node The node
	 * @return Bounding volume for the node.
	 */
	template <class NodeType>
	[[nodiscard]] geometry::AAEBB boundingVolume(NodeType const& node) const
	{
		if constexpr (std::is_same_v<NodeBV, NodeType>) {
			return node.boundingVolume();

		} else {
			return geometry::AAEBB(center(node), size(node) / 2);
		}
	}

	//
	// At
	//

	/*!
	 * @brief Get the node corresponding to a code with bounds checking.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNodeChecked' function can be used.
	 * The data inside the nodes returned by this function and 'createNodeChecked' will be
	 * the same, so it is only neccessary to use 'createNodeChecked' if you intend to alter
	 * what the node stores.
	 *
	 * @param code The code.
	 * @return The node.
	 */
	[[nodiscard]] std::optional<Node> at(Code code) const
	{
		return code.depth() <= rootDepth() ? std::optional<Node>(operator()(code))
		                                   : std::nullopt;
	}

	/*!
	 * @brief Get the node corresponding to a key with bounds checking.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNodeChecked' function can be used.
	 * The data inside the nodes returned by this function and 'createNodeChecked' will be
	 * the same, so it is only neccessary to use 'createNodeChecked' if you intend to alter
	 * what the node stores.
	 *
	 * @param key The key.
	 * @return The node.
	 */
	[[nodiscard]] std::optional<Node> at(Key key) const { return at(toCode(key)); }

	/*!
	 * @brief Get the node corresponding to a coordinate at a specific depth with bounds
	 * checking.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNodeChecked' function can be used.
	 * The data inside the nodes returned by this function and 'createNodeChecked' will be
	 * the same, so it is only neccessary to use 'createNodeChecked' if you intend to alter
	 * what the node stores.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The node.
	 */
	[[nodiscard]] std::optional<Node> at(Point coord, depth_t depth = 0) const
	{
		if (auto code = toCodeChecked(coord, depth)) {
			return std::optional<Node>(*code);
		} else {
			return std::nullopt;
		}
	}

	/*!
	 * @brief Get the node corresponding to a coordinate at a specific depth with bounds
	 * checking.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNodeChecked' function can be used.
	 * The data inside the nodes returned by this function and 'createNodeChecked' will be
	 * the same, so it is only neccessary to use 'createNodeChecked' if you intend to alter
	 * what the node stores.
	 *
	 * @param x,y,z The coordinate.
	 * @param depth The depth.
	 * @return The node.
	 */
	[[nodiscard]] std::optional<Node> at(coord_t x, coord_t y, coord_t z,
	                                     depth_t depth = 0) const
	{
		return at(Point(x, y, z), depth);
	}

	//
	// Function call operator
	//

	// TODO: Add comment
	[[nodiscard]] Node operator()(Node node) const
	{
		if constexpr (TrackNodes) {
			// TODO: Implement
		} else {
			// TODO: Implement
		}

		// TODO: Implement
		// if (!exists(leafNodeUnsafe(node))) {
		// 	return operator()(node.code());
		// } else if (node.isActualData() || isLeaf(node)) {
		// 	return node;
		// }

		if (node.isActualData()) {
			return node;
		}

		InnerNode const* n = &innerNodeUnsafe(node);
		depth_t depth = node.dataDepth();
		depth_t min_depth = std::max(node.depth(), depth_t(1));
		Code code = node.code();
		auto index = code.index(depth);
		while (min_depth != depth && !n->leaf[index]) {
			n = &innerChild(*n, index);
			--depth;
			index = code.index(depth);
		}

		return 0 == code.depth() && !n->leaf[index]
		           ? Node(const_cast<LeafNode*>(&leafChild(*n, index)), code, 0)
		           : Node(const_cast<InnerNode*>(n), code, depth);
	}

	/*!
	 * @brief Get the node corresponding to a code.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNode' function can be used. The
	 * data inside the nodes returned by this function and 'createNode' will be the same, so
	 * it is only neccessary to use 'createNode' if you intend to alter what the node
	 * stores.
	 *
	 * @param code The code.
	 * @return The node.
	 */
	[[nodiscard]] Node operator()(Code code) const
	{
		auto [node, depth] = leafNodeAndDepth(code);
		return Node(const_cast<LeafNode*>(&node), code, depth);
	}

	/*!
	 * @brief Get the node corresponding to a key.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNode' function can be used. The
	 * data inside the nodes returned by this function and 'createNode' will be the same, so
	 * it is only neccessary to use 'createNode' if you intend to alter what the node
	 * stores.
	 *
	 * @param key The key.
	 * @return The node.
	 */
	[[nodiscard]] Node operator()(Key key) const { return operator()(toCode(key)); }

	/*!
	 * @brief Get the node corresponding to a coordinate at a specific depth.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNode' function can be used. The
	 * data inside the nodes returned by this function and 'createNode' will be the same, so
	 * it is only neccessary to use 'createNode' if you intend to alter what the node
	 * stores.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The node.
	 */
	[[nodiscard]] Node operator()(Point coord, depth_t depth = 0) const
	{
		return operator()(toCode(coord, depth));
	}

	/*!
	 * @brief Get the node corresponding to a coordinate at a specific depth.
	 *
	 * @note The node can be higher up the tree than the specified depth. This happens if
	 * the node at a higher depth has no children. If it is neccessary that the node is at
	 * the specified depth, then the corresponding 'createNode' function can be used. The
	 * data inside the nodes returned by this function and 'createNode' will be the same, so
	 * it is only neccessary to use 'createNode' if you intend to alter what the node
	 * stores.
	 *
	 * @param x,y,z The coordinate.
	 * @param depth The depth.
	 * @return The node.
	 */
	[[nodiscard]] Node operator()(coord_t x, coord_t y, coord_t z, depth_t depth = 0) const
	{
		return operator()(toCode(x, y, z, depth));
	}

	//
	// Sibling
	//

	/*!
	 * @brief Get the sibling of a node.
	 *
	 * @param node The node.
	 * @param sibling_index The index of the sibling.
	 * @return The sibling.
	 */
	[[nodiscard]] Node sibling(Node node, index_t sibling_index) const
	{
		node = operator()(node);
		return Node(node.data(), node.code().sibling(sibling_index), node.dataDepth());
	}

	/*!
	 * @brief Get the sibling of a node.
	 *
	 * @param node The node.
	 * @param sibling_index The index of the sibling.
	 * @return The sibling.
	 */
	[[nodiscard]] NodeBV sibling(NodeBV const& node, index_t sibling_index) const
	{
		geometry::AAEBB aaebb(
		    siblingCenter(node.center(), node.halfSize(), node.index(), sibling_index),
		    node.halfSize());
		return NodeBV(sibling(static_cast<Node>(node), sibling_index), aaebb);
	}

	/*!
	 * @brief Get the sibling of a node with bounds checking.
	 *
	 * @param node The node.
	 * @param sibling_index The index of the sibling.
	 * @return The sibling.
	 */
	template <class Node>
	[[nodiscard]] Node siblingChecked(Node const& node, index_t sibling_index) const
	{
		if (!isRoot(node)) {
			throw std::out_of_range("Node has no siblings");
		} else if (7 < sibling_index) {
			throw std::out_of_range("sibling_index out of range");
		}
		return sibling(node, sibling_index);
	}

	//
	// Child
	//

	/*!
	 * @brief Get a child of a node.
	 *
	 * @param node The node.
	 * @param child_index The index of the child.
	 * @return The child.
	 */
	[[nodiscard]] Node child(Node node, index_t child_index) const
	{
		node = operator()(node);
		return isLeaf(node)
		           ? Node(node.data(), node.code().child(child_index), node.dataDepth())
		           : Node(const_cast<LeafNode*>(
		                      &child(innerNode(node), node.index(), node.depth())),
		                  node.code().child(child_index), node.dataDepth() - 1);
	}

	/*!
	 * @brief Get a child of a node.
	 *
	 * @param node The node.
	 * @param child_index The index of the child.
	 * @return The child.
	 */
	[[nodiscard]] NodeBV child(NodeBV const& node, index_t child_index) const
	{
		auto const child_half_size = node.halfSize() / 2;
		geometry::AAEBB child_aaebb(childCenter(node.center(), child_half_size, child_index),
		                            child_half_size);

		return NodeBV(child(static_cast<Node>(node), child_index), child_aaebb);
	}

	/*!
	 * @brief Get a child of a node with bounds checking.
	 *
	 * @param node The node.
	 * @param child_index The index of the child.
	 * @return The child.
	 */
	template <class Node>
	[[nodiscard]] Node childChecked(Node const& node, index_t child_index) const
	{
		if (!isParent(node)) {
			throw std::out_of_range("Node has no children");
		} else if (7 < child_index) {
			throw std::out_of_range("child_index out of range");
		}
		return child(node, child_index);
	}

	//
	// Parent
	//

	/*!
	 * @brief Get the parent of a node.
	 *
	 * @param node The node.
	 * @return The parent.
	 */
	[[nodiscard]] Node parent(Node node) const
	{
		return node.depth() == node.dataDepth() ? operator()(node.code().parent())
		                                        : Node(node.data(), node.code().parent(),
		                                               node.dataDepth());
	}

	/*!
	 * @brief Get the parent of a node.
	 *
	 * @param node The node.
	 * @return The parent.
	 */
	[[nodiscard]] NodeBV parent(NodeBV const& node) const
	{
		return toNodeBV(parent(static_cast<Node>(node)));
	}

	/*!
	 * @brief Get the parent of a node with bounds checking.
	 *
	 * @param node The node.
	 * @return The parent.
	 */
	template <class Node>
	[[nodiscard]] Node parentChecked(Node const& node) const
	{
		if (rootDepth() <= node.depth()) {
			throw std::out_of_range("Node has no parent");
		}
		return parent(node);
	}

	//
	// NodeBV
	//

	/*!
	 * @brief Convert a node to a node with bounding volume.
	 *
	 * @param node The node.
	 * @return The node with bounding volume.
	 */
	[[nodiscard]] NodeBV toNodeBV(Node node) const
	{
		return NodeBV(node, boundingVolume(node));
	}

	/**************************************************************************************
	|                                                                                     |
	|                                     Conversion                                      |
	|                                                                                     |
	**************************************************************************************/

	//
	// Code
	//

	/*!
	 * @brief Convert a key to a code.
	 *
	 * @param key The key to convert.
	 * @return The code corresponding to the key.
	 */
	[[nodiscard]] static constexpr Code toCode(Key key) noexcept { return Code(key); }

	/*!
	 * @brief Convert a coordinate at a specific depth to a code.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The code corresponding to the coordinate at the specific depth.
	 */
	[[nodiscard]] constexpr Code toCode(Point coord, depth_t depth = 0) const noexcept
	{
		return toCode(toKey(coord, depth));
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a code.
	 *
	 * @param x,y,z The coordinate.
	 * @param depth The depth.
	 * @return The code corresponding to the coordinate at the specific depth.
	 */
	[[nodiscard]] constexpr Code toCode(coord_t x, coord_t y, coord_t z,
	                                    depth_t depth = 0) const noexcept
	{
		return toCode(toKey(x, y, z, depth));
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a code with bounds check.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The code corresponding to the coordinate at the specific depth.
	 */
	[[nodiscard]] constexpr std::optional<Code> toCodeChecked(
	    Point coord, depth_t depth = 0) const noexcept
	{
		std::optional<key_t> key = toKeyChecked(coord, depth);
		return key ? std::optional<Code>(toCode(*key)) : std::nullopt;
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a code with bounds check.
	 *
	 * @param x,y,z The coordinate.
	 * @param depth The depth.
	 * @return The code corresponding to the coordinate at the specific depth.
	 */
	[[nodiscard]] constexpr std::optional<Code> toCodeChecked(
	    coord_t x, coord_t y, coord_t z, depth_t depth = 0) const noexcept
	{
		return toCodeChecked(Point(x, y, z), depth);
	}

	//
	// Key
	//

	/*!
	 * @brief Convert a code to a key.
	 *
	 * @param code The code to convert.
	 * @return The key corresponding to the code.
	 */
	[[nodiscard]] static constexpr Key toKey(Code code) noexcept { return code; }

	/*!
	 * @brief COnvert a coordinate at a specific depth to a key.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The corresponding key.
	 */
	[[nodiscard]] constexpr Key toKey(Point coord, depth_t depth = 0) const noexcept
	{
		return Key(toKey(coord.x, depth), toKey(coord.y, depth), toKey(coord.z, depth),
		           depth);
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a key.
	 *
	 * @param x,y,z The coordinate.
	 * @param depth The depth.
	 * @return The corresponding key.
	 */
	[[nodiscard]] constexpr Key toKey(coord_t x, coord_t y, coord_t z,
	                                  depth_t depth = 0) const noexcept
	{
		return Key(toKey(x, depth), toKey(y, depth), toKey(z, depth), depth);
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a key with bounds check.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The corresponding key.
	 */
	[[nodiscard]] constexpr std::optional<Key> toKeyChecked(
	    Point coord, depth_t depth = 0) const noexcept
	{
		return rootDepth() >= depth && isWithin(coord)
		           ? std::optional<Key>(toKey(coord, depth))
		           : std::nullopt;
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a key with bounds check.
	 *
	 * @param x,y,z The coordinate.
	 * @param depth The depth.
	 * @return The corresponding key.
	 */
	[[nodiscard]] constexpr std::optional<Key> toKeyChecked(
	    coord_t x, coord_t y, coord_t z, depth_t depth = 0) const noexcept
	{
		return toKeyChecked(Point(x, y, z), depth);
	}

	//
	// Coordinate
	//

	/*!
	 * @brief Convert a code to a coordinate.
	 *
	 * @param code The code.
	 * @return The corresponding coordinate.
	 */
	[[nodiscard]] constexpr Point toCoord(Code code) const noexcept
	{
		return toCoord(toKey(code));
	}

	/*!
	 * @brief Convert a key to a coordinate.
	 *
	 * @param key The key.
	 * @return The corresponding coordinate.
	 */
	[[nodiscard]] constexpr Point toCoord(Key key) const noexcept
	{
		return Point(toCoord(key[0], key.depth()), toCoord(key[1], key.depth()),
		             toCoord(key[2], key.depth()));
	}

	/*!
	 * @brief Convert a code to a coordinate with bounds check.
	 *
	 * @param Code The code.
	 * @return The corresponding coordinate.
	 */
	[[nodiscard]] constexpr std::optional<Point> toCoordChecked(Code code) const noexcept
	{
		return toCoordChecked(toKey(code));
	}

	/*!
	 * @brief Convert a key to a coordinate with bounds check.
	 *
	 * @param key The key.
	 * @return The corresponding coordinate.
	 */
	[[nodiscard]] constexpr std::optional<Point> toCoordChecked(Key key) const noexcept
	{
		return rootDepth() >= key.depth() ? std::optional<Point>(toCoord(key)) : std::nullopt;
	}

	/**************************************************************************************
	|                                                                                     |
	|                                      Traverse                                       |
	|                                                                                     |
	**************************************************************************************/

	/*!
	 * @brief Depth first traversal of the octree, starting at the root node. The function
	 * 'f' will be called for each node traverse. If 'f' returns false then the children of
	 * the node will also be traverse, otherwise they will not.
	 *
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class UnaryFunction>
	void traverse(UnaryFunction f) const
	{
		// FIXME: Should it be const also?
		if constexpr (std::is_same_v<Node, util::argument<UnaryFunction, 0>>) {
			traverseRecurs(rootNode(), f);
		} else {
			traverseRecurs(rootNodeBV(), f);
		}
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node. The function 'f'
	 * will be called for each node traverse. If 'f' returns false then the children of the
	 * node will also be traverse, otherwise they will not.
	 *
	 * @param node The node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class UnaryFunction>
	void traverse(Node node, UnaryFunction f) const
	{
		// FIXME: Should it be const also?
		if constexpr (std::is_same_v<util::argument<UnaryFunction, 0>, Node>) {
			traverseRecurs(node, f);
		} else {
			traverseRecurs(NodeBV(node, boundingVolume(node)), f);
		}
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * code. The function 'f' will be called for each node traverse. If 'f' returns false
	 * then the children of the node will also be traverse, otherwise they will not.
	 *
	 * @param code The code to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class UnaryFunction>
	void traverse(Code code, UnaryFunction f) const
	{
		traverse(operator()(code), f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * key. The function 'f' will be called for each node traverse. If 'f' returns false
	 * then the children of the node will also be traverse, otherwise they will not.
	 *
	 * @param key The key to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class UnaryFunction>
	void traverse(Key key, UnaryFunction f) const
	{
		traverse(toCode(key), f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * coordinate at a specified depth. The function 'f' will be called for each node
	 * traverse. If 'f' returns false then the children of the node will also be traverse,
	 * otherwise they will not.
	 *
	 * @param coord The coord to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 * @param depth The depth of the node.
	 */
	template <class UnaryFunction>
	void traverse(Point coord, UnaryFunction f, depth_t depth = 0) const
	{
		traverse(toCode(coord, depth), f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * coordinate at a specified depth. The function 'f' will be called for each node
	 * traverse. If 'f' returns false then the children of the node will also be traverse,
	 * otherwise they will not.
	 *
	 * @param x,y,z The coord to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 * @param depth The depth of the node.
	 */
	template <class UnaryFunction>
	void traverse(coord_t x, coord_t y, coord_t z, UnaryFunction f, depth_t depth = 0) const
	{
		traverse(toCode(x, y, z, depth), f);
	}

	/*! TODO: Update info for all nearest
	 * @brief Traverse the octree in the orderDepth first traversal of the octree, starting
	 * at the root node. The function 'f' will be called for each node traverse. If 'f'
	 * returns false then the children of the node will also be traverse, otherwise they
	 * will not.
	 *
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class Geometry, class UnaryFunction>
	void traverseNearest(Geometry const& g, UnaryFunction f) const
	{
		traverseNearestRecurs(rootNodeBV(), g, f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node. The function 'f'
	 * will be called for each node traverse. If 'f' returns false then the children of the
	 * node will also be traverse, otherwise they will not.
	 *
	 * @param node The node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class Geometry, class UnaryFunction>
	void traverseNearest(Node node, Geometry const& g, UnaryFunction f) const
	{
		traverseNearestRecurs(NodeBV(node, boundingVolume(node)), g, f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * code. The function 'f' will be called for each node traverse. If 'f' returns false
	 * then the children of the node will also be traverse, otherwise they will not.
	 *
	 * @param code The code to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class Geometry, class UnaryFunction>
	void traverseNearest(Code code, Geometry const& g, UnaryFunction f) const
	{
		traverseNearest(operator()(code), g, f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * key. The function 'f' will be called for each node traverse. If 'f' returns false
	 * then the children of the node will also be traverse, otherwise they will not.
	 *
	 * @param key The key to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 */
	template <class Geometry, class UnaryFunction>
	void traverseNearest(Key key, Geometry const& g, UnaryFunction f) const
	{
		traverseNearest(toCode(key), g, f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * coordinate at a specified depth. The function 'f' will be called for each node
	 * traverse. If 'f' returns false then the children of the node will also be traverse,
	 * otherwise they will not.
	 *
	 * @param coord The coord to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 * @param depth The depth of the node.
	 */
	template <class Geometry, class UnaryFunction>
	void traverseNearest(Point coord, Geometry const& g, UnaryFunction f,
	                     depth_t depth = 0) const
	{
		traverseNearest(toCode(coord, depth), g, f);
	}

	/*!
	 * @brief Depth first traversal of the octree, starting at the node corresponding to the
	 * coordinate at a specified depth. The function 'f' will be called for each node
	 * traverse. If 'f' returns false then the children of the node will also be traverse,
	 * otherwise they will not.
	 *
	 * @param x,y,z The coord to the node where to start the traversal.
	 * @param f The callback function to be called for each node traversed.
	 * @param depth The depth of the node.
	 */
	template <class Geometry, class UnaryFunction>
	void traverseNearest(coord_t x, coord_t y, coord_t z, Geometry const& g,
	                     UnaryFunction f, depth_t depth = 0) const
	{
		traverseNearest(toCode(x, y, z, depth), g, f);
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Query                                        |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	//
	// Query
	//

	template <class Predicates>
	[[nodiscard]] Query query(Predicates&& predicates, bool early_stopping = false) const
	{
		return Query(beginQuery(std::forward<Predicates>(predicates), early_stopping),
		             endQuery());
	}

	template <class Predicates>
	[[nodiscard]] Query query(Node node, Predicates&& predicates,
	                          bool early_stopping = false) const
	{
		return Query(beginQuery(node, std::forward<Predicates>(predicates), early_stopping),
		             endQuery());
	}

	template <class Predicates>
	[[nodiscard]] Query query(Code code, Predicates&& predicates,
	                          bool early_stopping = false) const
	{
		return Query(beginQuery(code, std::forward<Predicates>(predicates), early_stopping),
		             endQuery());
	}

	template <class Predicates>
	[[nodiscard]] Query query(Key key, Predicates&& predicates,
	                          bool early_stopping = false) const
	{
		return query(toCode(key), std::forward<Predicates>(predicates), early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] Query query(Point coord, depth_t depth, Predicates&& predicates,
	                          bool early_stopping = false) const
	{
		return query(toCode(coord, depth), std::forward<Predicates>(predicates),
		             early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] Query query(coord_t x, coord_t y, coord_t z, depth_t depth,
	                          Predicates&& predicates, bool early_stopping = false) const
	{
		return query(toCode(x, y, z, depth), std::forward<Predicates>(predicates),
		             early_stopping);
	}

	//
	// Query bounding volume
	//

	template <class Predicates>
	[[nodiscard]] QueryBV queryBV(Predicates&& predicates,
	                              bool early_stopping = false) const
	{
		return QueryBV(beginQueryBV(std::forward<Predicates>(predicates), early_stopping),
		               endQueryBV());
	}

	template <class Predicates>
	[[nodiscard]] QueryBV queryBV(Node node, Predicates&& predicates,
	                              bool early_stopping = false) const
	{
		return QueryBV(
		    beginQueryBV(node, std::forward<Predicates>(predicates), early_stopping),
		    endQueryBV());
	}

	template <class Predicates>
	[[nodiscard]] QueryBV queryBV(Code code, Predicates&& predicates,
	                              bool early_stopping = false) const
	{
		return QueryBV(
		    beginQueryBV(code, std::forward<Predicates>(predicates), early_stopping),
		    endQueryBV());
	}

	template <class Predicates>
	[[nodiscard]] QueryBV queryBV(Key key, Predicates&& predicates,
	                              bool early_stopping = false) const
	{
		return queryBV(toCode(key), std::forward<Predicates>(predicates), early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] QueryBV queryBV(Point coord, depth_t depth, Predicates&& predicates,
	                              bool early_stopping = false) const
	{
		return queryBV(toCode(coord, depth), std::forward<Predicates>(predicates),
		               early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] QueryBV queryBV(coord_t x, coord_t y, coord_t z, depth_t depth,
	                              Predicates&& predicates,
	                              bool early_stopping = false) const
	{
		return queryBV(toCode(x, y, z, depth), std::forward<Predicates>(predicates),
		               early_stopping);
	}

	//
	// Query nearest
	//

	template <class Geometry, class Predicates>
	[[nodiscard]] QueryNearest queryNearest(Geometry&& geometry, Predicates&& predicates,
	                                        bool early_stopping = false) const
	{
		return QueryNearest(
		    beginQueryNearest(std::forward<Geometry>(geometry),
		                      std::forward<Predicates>(predicates), early_stopping),
		    endQueryNearest());
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] QueryNearest queryNearest(Node node, Geometry&& geometry,
	                                        Predicates&& predicates,
	                                        bool early_stopping = false) const
	{
		return QueryNearest(
		    beginQueryNearest(node, std::forward<Geometry>(geometry),
		                      std::forward<Predicates>(predicates), early_stopping),
		    endQueryNearest());
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] QueryNearest queryNearest(Code code, Geometry&& geometry,
	                                        Predicates&& predicates,
	                                        bool early_stopping = false) const
	{
		return QueryNearest(
		    beginQueryNearest(code, std::forward<Geometry>(geometry),
		                      std::forward<Predicates>(predicates), early_stopping),
		    endQueryNearest());
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] QueryNearest queryNearest(Key key, Geometry&& geometry,
	                                        Predicates&& predicates,
	                                        bool early_stopping = false) const
	{
		return queryNearest(toCode(key), std::forward<Geometry>(geometry),
		                    std::forward<Predicates>(predicates), early_stopping);
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] QueryNearest queryNearest(Point coord, depth_t depth, Geometry&& geometry,
	                                        Predicates&& predicates,
	                                        bool early_stopping = false) const
	{
		return queryNearest(toCode(coord, depth), std::forward<Geometry>(geometry),
		                    std::forward<Predicates>(predicates), early_stopping);
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] QueryNearest queryNearest(coord_t x, coord_t y, coord_t z, depth_t depth,
	                                        Geometry&& geometry, Predicates&& predicates,
	                                        bool early_stopping = false) const
	{
		return queryNearest(toCode(x, y, z, depth), std::forward<Geometry>(geometry),
		                    std::forward<Predicates>(predicates), early_stopping);
	}

	//
	// Query to output
	//

	template <class Predicates, class OutputIt>
	OutputIt query(Predicates&& predicates, OutputIt d_first,
	               bool early_stopping = false) const
	{
		if constexpr (std::is_same_v<typename OutputIt::value_type, Node>) {
			return std::copy(beginQuery(std::forward<Predicates>(predicates), early_stopping),
			                 endQuery(), d_first);
		} else {
			return std::copy(beginQueryBV(std::forward<Predicates>(predicates), early_stopping),
			                 endQueryBV(), d_first);
		}
	}

	template <class Predicates, class OutputIt>
	OutputIt query(Node node, Predicates&& predicates, OutputIt d_first,
	               bool early_stopping = false) const
	{
		if constexpr (std::is_same_v<typename OutputIt::value_type, Node>) {
			return std::copy(
			    beginQuery(node, std::forward<Predicates>(predicates), early_stopping),
			    endQuery(), d_first);
		} else {
			return std::copy(
			    beginQueryBV(node, std::forward<Predicates>(predicates), early_stopping),
			    endQueryBV(), d_first);
		}
	}

	template <class Predicates, class OutputIt>
	OutputIt query(Code code, Predicates&& predicates, OutputIt d_first,
	               bool early_stopping = false) const
	{
		if constexpr (std::is_same_v<typename OutputIt::value_type, Node>) {
			return std::copy(
			    beginQuery(code, std::forward<Predicates>(predicates), early_stopping),
			    endQuery(), d_first);
		} else {
			return std::copy(
			    beginQueryBV(code, std::forward<Predicates>(predicates), early_stopping),
			    endQueryBV(), d_first);
		}
	}

	template <class Predicates, class OutputIt>
	OutputIt query(Key key, Predicates&& predicates, OutputIt d_first,
	               bool early_stopping = false) const
	{
		return query(toCode(key), std::forward<Predicates>(predicates), d_first,
		             early_stopping);
	}

	template <class Predicates, class OutputIt>
	OutputIt query(Point coord, depth_t depth, Predicates&& predicates, OutputIt d_first,
	               bool early_stopping = false) const
	{
		return query(toCode(coord, depth), std::forward<Predicates>(predicates), d_first,
		             early_stopping);
	}

	template <class Predicates, class OutputIt>
	OutputIt query(coord_t x, coord_t y, coord_t z, depth_t depth, Predicates&& predicates,
	               OutputIt d_first, bool early_stopping = false) const
	{
		return query(toCode(x, y, z, depth), std::forward<Predicates>(predicates), d_first,
		             early_stopping);
	}

	template <class Predicates, class OutputIt>
	OutputIt queryK(std::size_t k, Predicates&& predicates, OutputIt d_first,
	                bool early_stopping = false) const
	{
		std::size_t count = 0;
		if constexpr (std::is_same_v<typename OutputIt::value_type, Node>) {
			for (auto it = beginQuery(std::forward<Predicates>(predicates), early_stopping);
			     count < k && it != endQuery(); ++it, ++count) {
				*d_first++ = *it;
			}
		} else {
			for (auto it = beginQueryBV(std::forward<Predicates>(predicates), early_stopping);
			     count < k && it != endQueryBV(); ++it, ++count) {
				*d_first++ = *it;
			}
		}
		return d_first;
	}

	template <class Predicates, class OutputIt>
	OutputIt queryK(Node node, std::size_t k, Predicates&& predicates, OutputIt d_first,
	                bool early_stopping = false) const
	{
		std::size_t count = 0;
		if constexpr (std::is_same_v<typename OutputIt::value_type, Node>) {
			for (auto it =
			         beginQuery(node, std::forward<Predicates>(predicates), early_stopping);
			     count < k && it != endQuery(); ++it, ++count) {
				*d_first++ = *it;
			}
		} else {
			for (auto it =
			         beginQueryBV(node, std::forward<Predicates>(predicates), early_stopping);
			     count < k && it != endQueryBV(); ++it, ++count) {
				*d_first++ = *it;
			}
		}
		return d_first;
	}

	template <class Predicates, class OutputIt>
	OutputIt queryK(Code code, std::size_t k, Predicates&& predicates, OutputIt d_first,
	                bool early_stopping = false) const
	{
		std::size_t count = 0;
		if constexpr (std::is_same_v<typename OutputIt::value_type, Node>) {
			for (auto it =
			         beginQuery(code, std::forward<Predicates>(predicates), early_stopping);
			     count < k && it != endQuery(); ++it, ++count) {
				*d_first++ = *it;
			}
		} else {
			for (auto it =
			         beginQueryBV(code, std::forward<Predicates>(predicates), early_stopping);
			     count < k && it != endQueryBV(); ++it, ++count) {
				*d_first++ = *it;
			}
		}
		return d_first;
	}

	template <class Predicates, class OutputIt>
	OutputIt queryK(Key key, std::size_t k, Predicates&& predicates, OutputIt d_first,
	                bool early_stopping = false) const
	{
		return queryK(toCode(key), k, std::forward<Predicates>(predicates), d_first,
		              early_stopping);
	}

	template <class Predicates, class OutputIt>
	OutputIt queryK(Point coord, depth_t depth, std::size_t k, Predicates&& predicates,
	                OutputIt d_first, bool early_stopping = false) const
	{
		return queryK(toCode(coord, depth), k, std::forward<Predicates>(predicates), d_first,
		              early_stopping);
	}

	template <class Predicates, class OutputIt>
	OutputIt queryK(coord_t x, coord_t y, coord_t z, depth_t depth, std::size_t k,
	                Predicates&& predicates, OutputIt d_first,
	                bool early_stopping = false) const
	{
		return queryK(toCode(x, y, z, depth), k, std::forward<Predicates>(predicates),
		              d_first, early_stopping);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearest(Geometry&& geometry, Predicates&& predicates, OutputIt d_first,
	                      double epsilon = 0.0, bool early_stopping = false) const
	{
		return std::copy(
		    beginQueryNearest(std::forward<Geometry>(geometry),
		                      std::forward<Predicates>(predicates), epsilon, early_stopping),
		    endQueryNearest(), d_first);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearest(Node node, Geometry&& geometry, Predicates&& predicates,
	                      OutputIt d_first, double epsilon = 0.0,
	                      bool early_stopping = false) const
	{
		return std::copy(
		    beginQueryNearest(node, std::forward<Geometry>(geometry),
		                      std::forward<Predicates>(predicates), epsilon, early_stopping),
		    endQueryNearest(), d_first);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearest(Code code, Geometry&& geometry, Predicates&& predicates,
	                      OutputIt d_first, double epsilon = 0.0,
	                      bool early_stopping = false) const
	{
		return std::copy(
		    beginQueryNearest(code, std::forward<Geometry>(geometry),
		                      std::forward<Predicates>(predicates), epsilon, early_stopping),
		    endQueryNearest(), d_first);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearest(Key key, Geometry&& geometry, Predicates&& predicates,
	                      OutputIt d_first, double epsilon = 0.0,
	                      bool early_stopping = false) const
	{
		return queryNearest(toCode(key), std::forward<Geometry>(geometry),
		                    std::forward<Predicates>(predicates), d_first, epsilon,
		                    early_stopping);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearest(Point coord, depth_t depth, Geometry&& geometry,
	                      Predicates&& predicates, OutputIt d_first, double epsilon = 0.0,
	                      bool early_stopping = false) const
	{
		return queryNearest(toCode(coord, depth), std::forward<Geometry>(geometry),
		                    std::forward<Predicates>(predicates), d_first, epsilon,
		                    early_stopping);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearest(coord_t x, coord_t y, coord_t z, depth_t depth,
	                      Geometry&& geometry, Predicates&& predicates, OutputIt d_first,
	                      double epsilon = 0.0, bool early_stopping = false) const
	{
		return queryNearest(toCode(x, y, z, depth), std::forward<Geometry>(geometry),
		                    std::forward<Predicates>(predicates), d_first, epsilon,
		                    early_stopping);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearestK(std::size_t k, Geometry&& geometry, Predicates&& predicates,
	                       OutputIt d_first, double epsilon = 0.0,
	                       bool early_stopping = false) const
	{
		std::size_t count = 0;
		for (auto it = beginQueryNearest(std::forward<Geometry>(geometry),
		                                 std::forward<Predicates>(predicates), epsilon,
		                                 early_stopping);
		     count < k && it != endQueryNearest(); ++it, ++count) {
			*d_first++ = *it;
		}
		return d_first;
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearestK(Node node, std::size_t k, Geometry&& geometry,
	                       Predicates&& predicates, OutputIt d_first, double epsilon = 0.0,
	                       bool early_stopping = false) const
	{
		std::size_t count = 0;
		for (auto it = beginQueryNearest(node, std::forward<Geometry>(geometry),
		                                 std::forward<Predicates>(predicates), epsilon,
		                                 early_stopping);
		     count < k && it != endQueryNearest(); ++it, ++count) {
			*d_first++ = *it;
		}
		return d_first;
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearestK(Code code, std::size_t k, Geometry&& geometry,
	                       Predicates&& predicates, OutputIt d_first, double epsilon = 0.0,
	                       bool early_stopping = false) const
	{
		std::size_t count = 0;
		for (auto it = beginQueryNearest(code, std::forward<Geometry>(geometry),
		                                 std::forward<Predicates>(predicates), epsilon,
		                                 early_stopping);
		     count < k && it != endQueryNearest(); ++it, ++count) {
			*d_first++ = *it;
		}
		return d_first;
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearestK(Key key, std::size_t k, Geometry&& geometry,
	                       Predicates&& predicates, OutputIt d_first, double epsilon = 0.0,
	                       bool early_stopping = false) const
	{
		return queryNearestK(toCode(key), k, std::forward<Geometry>(geometry),
		                     std::forward<Predicates>(predicates), d_first, epsilon,
		                     early_stopping);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearestK(Point coord, depth_t depth, std::size_t k, Geometry&& geometry,
	                       Predicates&& predicates, OutputIt d_first, double epsilon = 0.0,
	                       bool early_stopping = false) const
	{
		return queryNearestK(toCode(coord, depth), k, std::forward<Geometry>(geometry),
		                     std::forward<Predicates>(predicates), d_first, epsilon,
		                     early_stopping);
	}

	template <class Geometry, class Predicates, class OutputIt>
	OutputIt queryNearestK(coord_t x, coord_t y, coord_t z, depth_t depth, std::size_t k,
	                       Geometry&& geometry, Predicates&& predicates, OutputIt d_first,
	                       double epsilon = 0.0, bool early_stopping = false) const
	{
		return queryNearestK(toCode(x, y, z, depth), k, std::forward<Geometry>(geometry),
		                     std::forward<Predicates>(predicates), d_first, epsilon,
		                     early_stopping);
	}

	//
	// Query iterator
	//

	template <class Predicates>
	[[nodiscard]] const_query_iterator beginQuery(Predicates&& predicates,
	                                              bool early_stopping = false) const
	{
		if (early_stopping) {
			if constexpr (predicate::contains_spatial_predicate_v<std::decay_t<Predicates>>) {
				return const_query_iterator(
				    new Iterator<Node, false, Derived, NodeBV, std::decay_t<Predicates>>(
				        &derived(), rootNodeBV(), std::forward<Predicates>(predicates)));
			} else {
				return const_query_iterator(
				    new Iterator<Node, false, Derived, Node, std::decay_t<Predicates>>(
				        &derived(), rootNode(), std::forward<Predicates>(predicates)));
			}
		} else {
			if constexpr (predicate::contains_spatial_predicate_v<std::decay_t<Predicates>>) {
				return const_query_iterator(
				    new Iterator<Node, true, Derived, NodeBV, std::decay_t<Predicates>>(
				        &derived(), rootNodeBV(), std::forward<Predicates>(predicates)));
			} else {
				return const_query_iterator(
				    new Iterator<Node, true, Derived, Node, std::decay_t<Predicates>>(
				        &derived(), rootNode(), std::forward<Predicates>(predicates)));
			}
		}
	}

	template <class Predicates>
	[[nodiscard]] const_query_iterator beginQuery(Node node, Predicates&& predicates,
	                                              bool early_stopping = false) const
	{
		if (early_stopping) {
			if constexpr (predicate::contains_spatial_predicate_v<std::decay_t<Predicates>>) {
				return const_query_iterator(
				    new Iterator<Node, false, Derived, NodeBV, std::decay_t<Predicates>>(
				        &derived(), toNodeBV(node), std::forward<Predicates>(predicates)));
			} else {
				return const_query_iterator(
				    new Iterator<Node, false, Derived, Node, std::decay_t<Predicates>>(
				        &derived(), node, std::forward<Predicates>(predicates)));
			}
		} else {
			if constexpr (predicate::contains_spatial_predicate_v<std::decay_t<Predicates>>) {
				return const_query_iterator(
				    new Iterator<Node, true, Derived, NodeBV, std::decay_t<Predicates>>(
				        &derived(), toNodeBV(node), std::forward<Predicates>(predicates)));
			} else {
				return const_query_iterator(
				    new Iterator<Node, true, Derived, Node, std::decay_t<Predicates>>(
				        &derived(), node, std::forward<Predicates>(predicates)));
			}
		}
	}

	template <class Predicates>
	[[nodiscard]] const_query_iterator beginQuery(Code code, Predicates&& predicates,
	                                              bool early_stopping = false) const
	{
		return beginQuery(operator()(code), std::forward<Predicates>(predicates),
		                  early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] const_query_iterator beginQuery(Key key, Predicates&& predicates,
	                                              bool early_stopping = false) const
	{
		return beginQuery(toCode(key), std::forward<Predicates>(predicates), early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] const_query_iterator beginQuery(Point coord, depth_t depth,
	                                              Predicates&& predicates,
	                                              bool early_stopping = false) const
	{
		return beginQuery(toCode(coord, depth), std::forward<Predicates>(predicates),
		                  early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] const_query_iterator beginQuery(coord_t x, coord_t y, coord_t z,
	                                              depth_t depth, Predicates&& predicates,
	                                              bool early_stopping = false) const
	{
		return beginQuery(toCode(x, y, z, depth), std::forward<Predicates>(predicates),
		                  early_stopping);
	}

	[[nodiscard]] const_query_iterator endQuery() const
	{
		return const_query_iterator(new Iterator<Node, true, Derived>(&derived()));
	}

	template <class Predicates>
	[[nodiscard]] const_bounding_volume_query_iterator beginQueryBV(
	    Predicates&& predicates, bool early_stopping = false) const
	{
		if (early_stopping) {
			return const_bounding_volume_query_iterator(
			    new Iterator<NodeBV, false, Derived, NodeBV, Predicates>(
			        &derived(), rootNodeBV(), std::forward<Predicates>(predicates)));
		} else {
			return const_bounding_volume_query_iterator(
			    new Iterator<NodeBV, true, Derived, NodeBV, Predicates>(
			        &derived(), rootNodeBV(), std::forward<Predicates>(predicates)));
		}
	}

	template <class Predicates>
	[[nodiscard]] const_bounding_volume_query_iterator beginQueryBV(
	    Node node, Predicates&& predicates, bool early_stopping = false) const
	{
		if (early_stopping) {
			return const_bounding_volume_query_iterator(
			    new Iterator<NodeBV, false, Derived, NodeBV, Predicates>(
			        &derived(), toNodeBV(node), std::forward<Predicates>(predicates)));
		} else {
			return const_bounding_volume_query_iterator(
			    new Iterator<NodeBV, true, Derived, NodeBV, Predicates>(
			        &derived(), toNodeBV(node), std::forward<Predicates>(predicates)));
		}
	}

	template <class Predicates>
	[[nodiscard]] const_bounding_volume_query_iterator beginQueryBV(
	    Code code, Predicates&& predicates, bool early_stopping = false) const
	{
		return beginQueryBV(operator()(code), std::forward<Predicates>(predicates),
		                    early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] const_bounding_volume_query_iterator beginQueryBV(
	    Key key, Predicates&& predicates, bool early_stopping = false) const
	{
		return beginQueryBV(toCode(key), std::forward<Predicates>(predicates),
		                    early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] const_bounding_volume_query_iterator beginQueryBV(
	    Point coord, depth_t depth, Predicates&& predicates,
	    bool early_stopping = false) const
	{
		return beginQueryBV(toCode(coord, depth), std::forward<Predicates>(predicates),
		                    early_stopping);
	}

	template <class Predicates>
	[[nodiscard]] const_bounding_volume_query_iterator beginQueryBV(
	    coord_t x, coord_t y, coord_t z, depth_t depth, Predicates&& predicates,
	    bool early_stopping = false) const
	{
		return beginQueryBV(toCode(x, y, z, depth), std::forward<Predicates>(predicates),
		                    early_stopping);
	}

	[[nodiscard]] const_bounding_volume_query_iterator endQueryBV() const
	{
		return const_bounding_volume_query_iterator(
		    new Iterator<NodeBV, true, Derived, NodeBV>(&derived()));
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] const_query_nearest_iterator beginQueryNearest(
	    Geometry&& geometry, Predicates&& predicates, double epsilon = 0.0,
	    bool early_stopping = false) const
	{
		if (early_stopping) {
			return const_query_nearest_iterator(
			    new NearestIterator<false, Derived, Geometry, Predicates>(
			        &derived(), rootNodeBV(), std::forward<Geometry>(geometry),
			        std::forward<Predicates>(predicates), epsilon));
		} else {
			return const_query_nearest_iterator(
			    new NearestIterator<true, Derived, Geometry, Predicates>(
			        &derived(), rootNodeBV(), std::forward<Geometry>(geometry),
			        std::forward<Predicates>(predicates), epsilon));
		}
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] const_query_nearest_iterator beginQueryNearest(
	    Node node, Geometry&& geometry, Predicates&& predicates, double epsilon = 0.0,
	    bool early_stopping = false) const
	{
		if (early_stopping) {
			return const_query_nearest_iterator(
			    new NearestIterator<false, Derived, Geometry, Predicates>(
			        &derived(), toNodeBV(node), std::forward<Geometry>(geometry),
			        std::forward<Predicates>(predicates), epsilon));
		} else {
			return const_query_nearest_iterator(
			    new NearestIterator<true, Derived, Geometry, Predicates>(
			        &derived(), toNodeBV(node), std::forward<Geometry>(geometry),
			        std::forward<Predicates>(predicates), epsilon));
		}
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] const_query_nearest_iterator beginQueryNearest(
	    Code code, Geometry&& geometry, Predicates&& predicates, double epsilon = 0.0,
	    bool early_stopping = false) const
	{
		return beginQueryNearest(operator()(code), std::forward<Geometry>(geometry),
		                         std::forward<Predicates>(predicates), epsilon,
		                         early_stopping);
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] const_query_nearest_iterator beginQueryNearest(
	    Key key, Geometry&& geometry, Predicates&& predicates, double epsilon = 0.0,
	    bool early_stopping = false) const
	{
		return beginQueryNearest(toCode(key), std::forward<Geometry>(geometry),
		                         std::forward<Predicates>(predicates), epsilon,
		                         early_stopping);
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] const_query_nearest_iterator beginQueryNearest(
	    Point coord, depth_t depth, Geometry&& geometry, Predicates&& predicates,
	    double epsilon = 0.0, bool early_stopping = false) const
	{
		return beginQueryNearest(toCode(coord, depth), std::forward<Geometry>(geometry),
		                         std::forward<Predicates>(predicates), epsilon,
		                         early_stopping);
	}

	template <class Geometry, class Predicates>
	[[nodiscard]] const_query_nearest_iterator beginQueryNearest(
	    coord_t x, coord_t y, coord_t z, depth_t depth, Geometry&& geometry,
	    Predicates&& predicates, double epsilon = 0.0, bool early_stopping = false) const
	{
		return beginQueryNearest(toCode(x, y, z, depth), std::forward<Geometry>(geometry),
		                         std::forward<Predicates>(predicates), epsilon,
		                         early_stopping);
	}

	[[nodiscard]] const_query_nearest_iterator endQueryNearest() const
	{
		return const_query_nearest_iterator(new NearestIterator<true, Derived>());
	}

	//
	// "Normal" iterator
	//

	[[nodiscard]] const_iterator begin(bool early_stopping = false) const
	{
		return beginQuery(predicate::Exists{}, early_stopping);
	}

	[[nodiscard]] const_iterator begin(Node node, bool early_stopping = false) const
	{
		return beginQuery(node, predicate::Exists{}, early_stopping);
	}

	[[nodiscard]] const_iterator begin(Code code, bool early_stopping = false) const
	{
		return beginQuery(code, predicate::Exists{}, early_stopping);
	}

	[[nodiscard]] const_iterator begin(Key key, bool early_stopping = false) const
	{
		return begin(toCode(key), early_stopping);
	}

	[[nodiscard]] const_iterator begin(Point coord, depth_t depth,
	                                   bool early_stopping = false) const
	{
		return begin(toCode(coord, depth), early_stopping);
	}

	[[nodiscard]] const_iterator begin(coord_t x, coord_t y, coord_t z, depth_t depth,
	                                   bool early_stopping = false) const
	{
		return begin(toCode(x, y, z, depth), early_stopping);
	}

	[[nodiscard]] const_iterator end() const { return endQuery(predicate::Exists{}); }

	[[nodiscard]] const_bounding_volume_iterator beginBV(bool early_stopping = false) const
	{
		return beginQueryBV(predicate::Exists{}, early_stopping);
	}

	[[nodiscard]] const_iterator beginBV(Node node, bool early_stopping = false) const
	{
		return beginQueryBV(node, predicate::Exists{}, early_stopping);
	}

	[[nodiscard]] const_iterator beginBV(Code code, bool early_stopping = false) const
	{
		return beginQueryBV(code, predicate::Exists{}, early_stopping);
	}

	[[nodiscard]] const_iterator beginBV(Key key, bool early_stopping = false) const
	{
		return beginBV(toCode(key), early_stopping);
	}

	[[nodiscard]] const_iterator beginBV(Point coord, depth_t depth,
	                                     bool early_stopping = false) const
	{
		return beginBV(toCode(coord, depth), early_stopping);
	}

	[[nodiscard]] const_iterator beginBV(coord_t x, coord_t y, coord_t z, depth_t depth,
	                                     bool early_stopping = false) const
	{
		return beginBV(toCode(x, y, z, depth), early_stopping);
	}

	[[nodiscard]] const_bounding_volume_iterator endBV() const
	{
		return endQueryBV(predicate::Exists{});
	}

	/**************************************************************************************
	|                                                                                     |
	|                                         I/O                                         |
	|                                                                                     |
	**************************************************************************************/

	void read(std::filesystem::path const& path, bool propagate = true)
	{
		std::ifstream file;
		file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		file.imbue(std::locale());
		file.open(path, std::ios_base::in | std::ios_base::binary);

		read(file, propagate);
	}

	void read(std::istream& in, bool propagate = true)
	{
		readData(in, readHeader(in), propagate);
	}

	void read(ReadBuffer& in, bool propagate = true)
	{
		readData(in, readHeader(in), propagate);
	}

	void readData(std::istream& in, FileHeader const& header, bool propagate = true)
	{
		if (size() != header.leaf_size || depthLevels() != header.depth_levels) {
			clear(header.leaf_size, header.depth_levels);
		}

		auto nodes = readNodes(in);
		derived().readNodes(in, std::begin(nodes), std::end(nodes), header.compressed);

		if (propagate) {
			propagateModified();
		}
	}

	void readData(ReadBuffer& in, FileHeader const& header, bool propagate = true)
	{
		if (size() != header.leaf_size || depthLevels() != header.depth_levels) {
			clear(header.leaf_size, header.depth_levels);
		}

		auto nodes = readNodes(in);
		derived().readNodes(in, std::begin(nodes), std::end(nodes), false, header.compressed);

		if (propagate) {
			propagateModified();
		}
	}

	void write(std::filesystem::path const& path, depth_t min_depth = 0,
	           bool compress = false, int compression_acceleration_level = 1,
	           int compression_level = 0) const
	{
		write(path, predicate::Exists(), min_depth, compress, compression_acceleration_level,
		      compression_level);
	}

	void write(std::ostream& out, depth_t min_depth = 0, bool compress = false,
	           int compression_acceleration_level = 1, int compression_level = 0) const
	{
		write(out, predicate::Exists(), min_depth, compress, compression_acceleration_level,
		      compression_level);
	}

	void write(WriteBuffer& out, depth_t min_depth = 0, bool compress = false,
	           int compression_acceleration_level = 1, int compression_level = 0) const
	{
		write(out, predicate::Exists(), min_depth, compress, compression_acceleration_level,
		      compression_level);
	}

	Buffer write(depth_t min_depth = 0, bool compress = false,
	             int compression_acceleration_level = 1, int compression_level = 0) const
	{
		return write(predicate::Exists(), min_depth, compress, compression_acceleration_level,
		             compression_level);
	}

	template <class Predicates,
	          typename = std::enable_if_t<!std::is_scalar_v<std::decay_t<Predicates>>>>
	void write(std::filesystem::path const& path, Predicates&& predicates,
	           depth_t min_depth = 0, bool compress = false,
	           int compression_acceleration_level = 1, int compression_level = 0) const
	{
		std::ofstream file;
		file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
		file.imbue(std::locale());
		file.open(path, std::ios_base::out | std::ios_base::binary);

		write(file, std::forward<Predicates>(predicates), min_depth, compress,
		      compression_acceleration_level, compression_level);
	}

	template <class Predicates,
	          typename = std::enable_if_t<!std::is_scalar_v<std::decay_t<Predicates>>>>
	void write(std::ostream& out, Predicates&& predicates, depth_t min_depth = 0,
	           bool compress = false, int compression_acceleration_level = 1,
	           int compression_level = 0) const
	{
		auto [tree_structure, nodes] =
		    data(predicate::Leaf() && predicate::DepthMin(min_depth) &&
		         std::forward<Predicates>(predicates));
		write(out, tree_structure, nodes, compress, compression_acceleration_level,
		      compression_level);
	}

	template <class Predicates,
	          typename = std::enable_if_t<!std::is_scalar_v<std::decay_t<Predicates>>>>
	void write(WriteBuffer& out, Predicates&& predicates, depth_t min_depth = 0,
	           bool compress = false, int compression_acceleration_level = 1,
	           int compression_level = 0) const
	{
		auto [tree_structure, nodes] =
		    data(predicate::Leaf() && predicate::DepthMin(min_depth) &&
		         std::forward<Predicates>(predicates));
		write(out, tree_structure, nodes, compress, compression_acceleration_level,
		      compression_level);
	}

	template <class Predicates,
	          typename = std::enable_if_t<!std::is_scalar_v<std::decay_t<Predicates>>>>
	Buffer write(Predicates&& predicates, depth_t min_depth = 0, bool compress = false,
	             int compression_acceleration_level = 1, int compression_level = 0) const
	{
		Buffer buffer;
		write(buffer, predicates, min_depth, compress, compression_acceleration_level,
		      compression_level);
		return buffer;
	}

	void writeModifiedAndPropagate(std::filesystem::path const& filename,
	                               bool compress = false,
	                               int compression_acceleration_level = 1,
	                               int compression_level = 0)
	{
		std::ofstream file;
		file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
		file.imbue(std::locale());
		file.open(filename, std::ios_base::out | std::ios_base::binary);

		writeModifiedAndPropagate(file, compress, compression_acceleration_level,
		                          compression_level);
	}

	void writeModifiedAndPropagate(std::ostream& out, bool compress = false,
	                               int compression_acceleration_level = 1,
	                               int compression_level = 0)
	{
		modifiedData<true>();
		write(out, modified_tree_, modified_nodes_, compress, compression_acceleration_level,
		      compression_level);
	}

	void writeModifiedAndPropagate(WriteBuffer& out, bool compress = false,
	                               int compression_acceleration_level = 1,
	                               int compression_level = 0)
	{
		modifiedData<true>();
		write(out, modified_tree_, modified_nodes_, compress, compression_acceleration_level,
		      compression_level);
	}

	Buffer writeModifiedAndPropagate(bool compress = false,
	                                 int compression_acceleration_level = 1,
	                                 int compression_level = 0)
	{
		Buffer buffer;
		writeModifiedAndPropagate(buffer, compress, compression_acceleration_level,
		                          compression_level);
		return buffer;
	}

	void writeModifiedAndReset(std::filesystem::path const& filename, bool compress = false,
	                           int compression_acceleration_level = 1,
	                           int compression_level = 0)
	{
		std::ofstream file;
		file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
		file.imbue(std::locale());
		file.open(filename, std::ios_base::out | std::ios_base::binary);

		writeModifiedAndReset(file, compress, compression_acceleration_level,
		                      compression_level);
	}

	void writeModifiedAndReset(std::ostream& out, bool compress = false,
	                           int compression_acceleration_level = 1,
	                           int compression_level = 0)
	{
		modifiedData<false>();
		write(out, modified_tree_, modified_nodes_, compress, compression_acceleration_level,
		      compression_level);
	}

	void writeModifiedAndReset(WriteBuffer& out, bool compress = false,
	                           int compression_acceleration_level = 1,
	                           int compression_level = 0)
	{
		modifiedData<false>();
		write(out, modified_tree_, modified_nodes_, compress, compression_acceleration_level,
		      compression_level);
	}

	Buffer writeModifiedAndReset(bool compress = false,
	                             int compression_acceleration_level = 1,
	                             int compression_level = 0)
	{
		Buffer buffer;
		writeModifiedAndReset(buffer, compress, compression_acceleration_level,
		                      compression_level);
		return buffer;
	}

	/**************************************************************************************
	|                                                                                     |
	|                                     Statistics                                      |
	|                                                                                     |
	**************************************************************************************/

	/*!
	 * @return Number of inner nodes in the octree.
	 */
	[[nodiscard]] constexpr std::size_t numInnerNodes() const noexcept
	{
		if constexpr (CountNodes) {
			return num_inner_nodes_;
		} else {
			// TODO: Implement
			return 0;
		}
	}

	/*!
	 * @return Number of inner leaf nodes in the octree.
	 */
	[[nodiscard]] constexpr std::size_t numInnerLeafNodes() const noexcept
	{
		if constexpr (CountNodes) {
			return num_inner_leaf_nodes_;
		} else {
			// TODO: Implement
			return 0;
		}
	}

	/*!
	 * @return Number of leaf nodes in the octree.
	 */
	[[nodiscard]] constexpr std::size_t numLeafNodes() const noexcept
	{
		if constexpr (CountNodes) {
			return num_leaf_nodes_;
		} else {
			// TODO: Implement
			return 0;
		}
	}

	/*!
	 * @return Number of nodes in the octree.
	 */
	[[nodiscard]] constexpr std::size_t numNodes() const noexcept
	{
		return numInnerNodes() + numInnerLeafNodes() + numLeafNodes();
	}

	/*!
	 * @brief This is lower bound memory usage of an inner node.
	 *
	 * @note Additional data accessed by pointers inside an inner node are not counted.
	 *
	 * @return Memory usage of a single inner node.
	 */
	[[nodiscard]] constexpr std::size_t memoryInnerNode() const noexcept
	{
		return sizeof(InnerNode) / 8;
	}

	/*!
	 * @brief This is lower bound memory usage of an inner leaf node.
	 *
	 * @note Additional data accessed by pointers inside an inner leaf node are not counted.
	 *
	 * @return Memory usage of a single inner leaf node.
	 */
	[[nodiscard]] constexpr std::size_t memoryInnerLeafNode() const noexcept
	{
		return sizeof(InnerNode) / 8;
	}

	/*!
	 * @brief This is lower bound memory usage of a leaf node.
	 *
	 * @note Additional data accessed by pointers inside a leaf node are not counted.
	 *
	 * @return Memory usage of a single leaf node.
	 */
	[[nodiscard]] constexpr std::size_t memoryLeafNode() const noexcept
	{
		return sizeof(LeafNode) / 8;
	}

	/*!
	 * @brief Lower bound memory usage for all nodes.
	 *
	 * @note Does not account for pointed to data inside nodes.
	 *
	 * @return Memory usage of the octree.
	 */
	[[nodiscard]] std::size_t memoryUsage() const noexcept
	{
		return (numInnerNodes() * memoryInnerNode()) +
		       (numInnerLeafNodes() * memoryInnerLeafNode()) +
		       (numLeafNodes() * memoryLeafNode());
	}

	/*!
	 * @return Number of allocated inner nodes.
	 */
	[[nodiscard]] constexpr std::size_t numInnerNodesAllocated() const noexcept
	{
		if constexpr (CountNodes) {
			return num_allocated_inner_nodes_;
		} else {
			// TODO: Implement
			return 0;
		}
	}

	/*!
	 * @return Number of allocated inner leaf nodes.
	 */
	[[nodiscard]] constexpr std::size_t numInnerLeafNodesAllocated() const noexcept
	{
		if constexpr (CountNodes) {
			return num_allocated_inner_leaf_nodes_;
		} else {
			// TODO: Implement
			return 0;
		}
	}

	/*!
	 * @return Number of allocated leaf nodes.
	 */
	[[nodiscard]] constexpr std::size_t numLeafNodesAllocated() const noexcept
	{
		if constexpr (CountNodes) {
			return num_allocated_leaf_nodes_;
		} else {
			// TODO: Implement
			return 0;
		}
	}

	/*!
	 * @return Number of allocated nodes.
	 */
	[[nodiscard]] constexpr std::size_t numNodesAllocated() const noexcept
	{
		return numInnerNodesAllocated() + numInnerLeafNodesAllocated() +
		       numLeafNodesAllocated();
	}

	/*!
	 * @brief Lower bound memory usage for all allocated nodes.
	 *
	 * @note Does not account for pointed to data inside nodes.
	 *
	 * @return Memory usage of the allocated octree.
	 */
	[[nodiscard]] std::size_t memoryUsageAllocated() const noexcept
	{
		return (numInnerNodesAllocated() * memoryInnerNode()) +
		       (numInnerLeafNodesAllocated() * memoryInnerLeafNode()) +
		       (numLeafNodesAllocated() * memoryLeafNode());
	}

 protected:
	/**************************************************************************************
	|                                                                                     |
	|                                    Constructors                                     |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	OctreeBase(node_size_t leaf_node_size = 0.1, depth_t depth_levels = 16,
	           bool auto_prune = false)
	    : automatic_prune_(auto_prune)
	{
		setNodeSizeAndDepthLevels(leaf_node_size, depth_levels);

		init();
	}

	OctreeBase(OctreeBase const& other)
	    : depth_levels_(other.depth_levels_),
	      max_value_(other.max_value_),
	      node_size_(other.node_size_),
	      node_size_factor_(other.node_size_factor_),
	      automatic_prune_(other.automatic_prune_)
	{
		init();
	}

	OctreeBase(OctreeBase&& other)
	    : depth_levels_(std::move(other.depth_levels_)),
	      max_value_(std::move(other.max_value_)),
	      root_(std::move(other.root_)),
	      node_size_(std::move(other.node_size_)),
	      node_size_factor_(std::move(other.node_size_factor_)),
	      automatic_prune_(std::move(other.automatic_prune_))
	{
		num_inner_nodes_.store(other.num_inner_nodes_);
		num_inner_leaf_nodes_.store(other.num_inner_leaf_nodes_);
		num_leaf_nodes_.store(other.num_leaf_nodes_);

		num_allocated_inner_nodes_.store(other.num_allocated_inner_nodes_);
		num_allocated_inner_leaf_nodes_.store(other.num_allocated_inner_leaf_nodes_);
		num_allocated_leaf_nodes_.store(other.num_allocated_leaf_nodes_);

		init();
	}

	template <class Derived2, class Data2, class InnerData2, bool ReuseNodes2,
	          UFOLock Lock2, bool CountNodes2>
	OctreeBase(OctreeBase<Derived2, Data2, InnerData2, ReuseNodes2, Lock2,
	                      CountNodes2> const& other)
	    : depth_levels_(other.depth_levels_),
	      max_value_(other.max_value_),
	      node_size_(other.node_size_),
	      node_size_factor_(other.node_size_factor_),
	      automatic_prune_(other.automatic_prune_)
	{
		init();
	}

	//
	// Init
	//

	void init()
	{
		for (auto& a_l : children_locks_) {
			a_l.clear();
		}
	}

	//
	// Destructor
	//

	~OctreeBase() { deleteChildren(root(), rootDepth(), true); }

	/**************************************************************************************
	|                                                                                     |
	|                                 Assignment operator                                 |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	OctreeBase& operator=(OctreeBase const& rhs)
	{
		// TODO: Should this clear?
		clear(rhs.size(), rhs.depthLevels());

		depth_levels_ = rhs.depth_levels_;
		max_value_ = rhs.max_value_;
		node_size_ = rhs.node_size_;
		node_size_factor_ = rhs.node_size_factor_;
		automatic_prune_ = rhs.automatic_prune_;
		return *this;
	}

	OctreeBase& operator=(OctreeBase&& rhs)
	{
		// TODO: Should this clear?
		clear(rhs.size(), rhs.depthLevels(), true);

		depth_levels_ = std::move(rhs.depth_levels_);
		max_value_ = std::move(rhs.max_value_);
		root_ = std::move(rhs.root_);
		node_size_ = std::move(rhs.node_size_);
		node_size_factor_ = std::move(rhs.node_size_factor_);
		automatic_prune_ = std::move(rhs.automatic_prune_);
		num_inner_nodes_.store(rhs.num_inner_nodes_);
		num_inner_leaf_nodes_.store(rhs.num_inner_leaf_nodes_);
		num_leaf_nodes_.store(rhs.num_leaf_nodes_);
		num_allocated_inner_nodes_.store(rhs.num_allocated_inner_nodes_);
		num_allocated_inner_leaf_nodes_.store(rhs.num_allocated_inner_leaf_nodes_);
		num_allocated_leaf_nodes_.store(rhs.num_allocated_leaf_nodes_);
		return *this;
	}

	template <class Derived2, class Data2, class InnerData2, bool ReuseNodes2,
	          UFOLock Lock2, bool CountNodes2>
	OctreeBase& operator=(
	    OctreeBase<Derived2, Data2, InnerData2, ReuseNodes2, Lock2, CountNodes2> const& rhs)
	{
		// TODO: Should this clear?
		clear(rhs.size(), rhs.depthLevels());

		depth_levels_ = rhs.depth_levels_;
		max_value_ = rhs.max_value_;
		node_size_ = rhs.node_size_;
		node_size_factor_ = rhs.node_size_factor_;
		automatic_prune_ = rhs.automatic_prune_;
		return *this;
	}

	/**************************************************************************************
	|                                                                                     |
	|                                         Swap                                        |
	|                                                                                     |
	**************************************************************************************/

	void swap(OctreeBase& other)
	{
		std::swap(depth_levels_, other.depth_levels_);
		std::swap(max_value_, other.max_value_);
		std::swap(root_, other.root_);
		std::swap(node_size_, other.node_size_);
		std::swap(node_size_factor_, other.node_size_factor_);
		std::swap(automatic_prune_, other.automatic_prune_);
		// TODO: Check locks
		std::swap(free_inner_blocks_, other.free_inner_blocks_);
		std::swap(free_leaf_blocks_, other.free_leaf_blocks_);

		num_inner_nodes_ = other.num_inner_nodes_.exchange(num_inner_nodes_);
		num_inner_leaf_nodes_ = other.num_inner_leaf_nodes_.exchange(num_inner_leaf_nodes_);
		num_leaf_nodes_ = other.num_leaf_nodes_.exchange(num_leaf_nodes_);
		num_allocated_inner_nodes_ =
		    other.num_allocated_inner_nodes_.exchange(num_allocated_inner_nodes_);
		num_allocated_inner_leaf_nodes_ =
		    other.num_allocated_inner_leaf_nodes_.exchange(num_allocated_inner_leaf_nodes_);
		num_allocated_leaf_nodes_ =
		    other.num_allocated_leaf_nodes_.exchange(num_allocated_leaf_nodes_);
	}

	/**************************************************************************************
	|                                                                                     |
	|                                       Derived                                       |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	[[nodiscard]] constexpr Derived& derived() { return *static_cast<Derived*>(this); }

	[[nodiscard]] constexpr Derived const& derived() const
	{
		return *static_cast<Derived const*>(this);
	}

	/**************************************************************************************
	|                                                                                     |
	|                                       Octree                                        |
	|                                                                                     |
	**************************************************************************************/

	void setNodeSizeAndDepthLevels(node_size_t const leaf_node_size,
	                               depth_t const depth_levels)
	{
		if (minDepthLevels() > depth_levels || maxDepthLevels() < depth_levels) {
			throw std::invalid_argument("depth_levels have to be in range [" +
			                            std::to_string(+minDepthLevels()) + ".." +
			                            std::to_string(+maxDepthLevels()) + "], '" +
			                            std::to_string(+depth_levels) + "' was supplied.");
		}

		depth_levels_ = depth_levels;
		max_value_ = std::pow(2, depth_levels - 2);

		// For increased precision
		node_size_[0] = leaf_node_size;
		int const s = node_size_.size();
		for (int i = 1; s != i; ++i) {
			node_size_[i] = std::ldexp(leaf_node_size, i);
		}

		std::transform(std::cbegin(node_size_), std::cend(node_size_),
		               std::begin(node_size_factor_), [](auto const n) { return 1.0 / n; });
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Root                                         |
	|                                                                                     |
	**************************************************************************************/

	/*!
	 * @brief Initilize the root node.
	 */
	void initRoot()
	{
		root().leaf.set();
		root().modified.reset();
		// TODO: Implement
		// if constexpr (ReuseNodes) {
		// 	root().code = rootCode();  // TODO: Correct?
		// } else {
		// 	root().exists = true;
		// }
	}

	/*!
	 * @brief Get the root node.
	 *
	 * @return The root node.
	 */
	[[nodiscard]] constexpr InnerNode const& root() const noexcept { return root_; }

	/*!
	 * @brief Get the root node.
	 *
	 * @return The root node.
	 */
	[[nodiscard]] constexpr InnerNode& root() noexcept { return root_; }

	/*!
	 * @brief Get the root node's index field.
	 *
	 * @return The root node's index field.
	 */
	[[nodiscard]] constexpr IndexField rootIndexField() const noexcept
	{
		return IndexField(1);
	}

	/*!
	 * @brief Get the root node index.
	 *
	 * @return The root node index.
	 */
	[[nodiscard]] constexpr index_t rootIndex() const noexcept { return index_t(0); }

	/**************************************************************************************
	|                                                                                     |
	|                                        Root                                         |
	|                                                                                     |
	**************************************************************************************/

	[[nodiscard]] static constexpr bool valid(Node node)
	{
		if constexpr (TrackNodes) {
			if constexpr (ReuseNodes) {
				return leafNodeUnsafe(node).code == node.dataCode().parent();
			} else {
				return leafNodeUnsafe(node).exists;
			}
		} else {
			return true;
		}
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Node                                         |
	|                                                                                     |
	**************************************************************************************/

	[[nodiscard]] index_t dataIndex(Node node) const { return node.dataIndex(); }

	/**************************************************************************************
	|                                                                                     |
	|                                        Apply                                        |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	template <class BinaryFunction, class UnaryFunction,
	          typename = std::enable_if_t<std::is_copy_constructible_v<BinaryFunction> &&
	                                      std::is_copy_constructible_v<UnaryFunction>>>
	Node apply(Node node, BinaryFunction f, UnaryFunction f2, bool const propagate)
	{
		if (!valid(node)) {
			return apply(node.code(), f, f2, propagate);
		}

		Node ret = node;

		if (node.isActualData()) {
			auto index = node.index();
			if (isPureLeaf(node)) {
				f(leafNodeUnsafe(node), index);
			} else {
				auto& inner_node = innerNodeUnsafe(node);
				if (std::as_const(inner_node).leaf[index]) {
					f(inner_node, index);
				} else {
					IndexField indices;
					indices[index] = true;
					applyAllRecurs(inner_node, indices, node.depth(), f, f2);
				}
			}
		} else {
			ret = apply(innerNodeUnsafe(node), node.dataDepth(), node.code(), f, f2);
		}

		if (leafNodeUnsafe(node).modified.none()) {
			setModifiedParents(node.dataCode());
		}

		leafNodeUnsafe(node).modified[node.dataIndex()] = true;

		if (propagate) {
			propagateModified();
			// TODO: Perhaps change node?
		}

		return ret;
	}

	template <class BinaryFunction, class UnaryFunction,
	          typename = std::enable_if_t<std::is_copy_constructible_v<BinaryFunction> &&
	                                      std::is_copy_constructible_v<UnaryFunction>>>
	Node apply(Code code, BinaryFunction f, UnaryFunction f2, bool const propagate)
	{
		// FIXME: Should this be here?
		if (code.depth() > rootDepth()) {
			return Node();
		}

		auto ret = apply(root(), rootDepth(), code, f, f2);

		if (propagate) {
			propagateModified();
			// TODO: Perhaps change node?
		}

		return ret;
	}

	template <class BinaryFunction, class UnaryFunction,
	          typename = std::enable_if_t<std::is_copy_constructible_v<BinaryFunction> &&
	                                      std::is_copy_constructible_v<UnaryFunction>>>
	Node apply(InnerNode& node, depth_t depth, Code code, BinaryFunction f,
	           UnaryFunction f2)
	{
		InnerNode* cur = &node;
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth) {
			createInnerChildren(*cur, index, depth);
			cur->modified[index] = true;
			cur = &innerChild(*cur, index);
			--depth;
			index = code.index(depth);
		}

		if (0 == code.depth()) {
			createLeafChildren(*cur, index);
			cur->modified[index] = true;
			LeafNode& child = leafChild(*cur, index);
			auto child_index = code.index(0);
			f(child, child_index);
			child.modified[child_index] = true;
			return Node(&child, code, 0);
		} else if (std::as_const(cur)->leaf[index]) {
			f(*cur, index);
		} else {
			IndexField indices;
			indices[index] = true;
			applyAllRecurs(*cur, indices, depth, f, f2);
		}

		cur->modified[index] = true;
		return Node(cur, code, depth);
	}

	template <class BinaryFunction, class UnaryFunction,
	          typename = std::enable_if_t<std::is_copy_constructible_v<BinaryFunction> &&
	                                      std::is_copy_constructible_v<UnaryFunction>>>
	void applyAllRecurs(InnerNode& node, IndexField const indices, depth_t depth,
	                    BinaryFunction f, UnaryFunction f2)
	{
		if (1 == depth) {
			for (std::size_t i = 0; 8 != i; ++i) {
				if (indices[i]) {
					auto& children = leafChild(node, i);
					f2(children);
					children.modified.set();
				}
			}
		} else {
			for (std::size_t i = 0; 8 != i; ++i) {
				if (indices[i]) {
					auto& children = innerChild(node, i);
					if (children.leaf.all()) {
						f2(children);
					} else {
						if (children.leaf.any()) {
							for (index_t j = 0; 8 != j; ++j) {
								if (std::as_const(children).leaf[j]) {
									f(static_cast<LeafNode&>(children), j);
								}
							}
						}
						applyAllRecurs(children, ~children.leaf, depth - 1, f, f2);
					}
					children.modified.set();
				}
			}
		}
	}

	/**************************************************************************************
	|                                                                                     |
	|                                      Traverse                                       |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comment

	template <class NodeType, class UnaryFunction>
	void traverseRecurs(NodeType const& node, UnaryFunction f) const
	{
		if (f(node) || isLeaf(node)) {
			return;
		}

		node = child(node, 0);
		for (index_t index = 0; 8 != index; ++index) {
			traverseRecurs(sibling(node, index), f);
		}
	}

	template <class Geometry, class UnaryFunction>
	void traverseNearestRecurs(NodeBV const& node, Geometry const& g, UnaryFunction f) const
	{
		// TODO: Implement
	}

	/**************************************************************************************
	|                                                                                     |
	|                                     Conversion                                      |
	|                                                                                     |
	**************************************************************************************/

	//
	// Key
	//

	/*!
	 * @brief Convert a coordinate at a specific depth to a key.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The corresponding key.
	 */
	[[nodiscard]] constexpr key_t toKey(coord_t coord, depth_t depth = 0) const
	{
		// FIXME: Make it possible to make a cloud of coordinates
		key_t val = std::floor(node_size_factor_[0] * coord);
		return ((val + max_value_) >> depth) << depth;
	}

	/*!
	 * @brief Convert a coordinate at a specific depth to a key with bounds check.
	 *
	 * @param coord The coordinate.
	 * @param depth The depth.
	 * @return The corresponding key.
	 */
	[[nodiscard]] constexpr std::optional<key_t> toKeyChecked(coord_t coord,
	                                                          depth_t depth = 0) const
	{
		auto min = -size(rootDepth() - 1);
		auto max = -min;
		return min <= coord && max >= coord ? std::optional<key_t>(toKey(coord, depth))
		                                    : std::nullopt;
	}

	//
	// Coordinate
	//

	/*!
	 * @brief Convert a key to a coordinate at a specific depth.
	 *
	 * @param key The key.
	 * @param depth The depth of the coordinate.
	 * @return The corresponding coordinate.
	 */
	[[nodiscard]] constexpr coord_t toCoord(key_t key, depth_t depth = 0) const
	{
		constexpr auto sub64 = std::minus<int_fast64_t>{};
		return rootDepth() == depth
		           ? 0
		           : (std::floor(sub64(key, max_value_) / static_cast<coord_t>(1U << depth)) +
		              coord_t(0.5)) *
		                 size(depth);
		//  FIXME: Look at
		// return rootDepth() == depth
		//            ? 0
		//            : ((sub64(key, max_value_) >> depth) + coord_t(0.5)) * size(depth);
	}

	/**************************************************************************************
	|                                                                                     |
	|                                    Access nodes                                     |
	|                                                                                     |
	**************************************************************************************/

	//
	// Get node
	//

	[[nodiscard]] static constexpr LeafNode& leafNodeUnsafe(Node node)
	{
		return *static_cast<LeafNode*>(node.data());
	}

	[[nodiscard]] LeafNode const& leafNode(Node node) const
	{
		return leafNodeUnsafe(operator()(node));
	}

	[[nodiscard]] LeafNode& leafNode(Node node) { return leafNodeUnsafe(operator()(node)); }

	[[nodiscard]] LeafNode const& leafNode(Code code) const
	{
		InnerNode const* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerNode(*node, index);
			--depth;
			index = code.index(depth);
		}

		return 0 == code.depth() && !node->leaf[index] ? leafNode(*node, index) : *node;
	}

	[[nodiscard]] LeafNode& leafNode(Code code)
	{
		InnerNode* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerNode(*node, index);
			--depth;
			index = code.index(depth);
		}

		return 0 == code.depth() && !node->leaf[index] ? leafNode(*node, index) : *node;
	}

	[[nodiscard]] static constexpr InnerNode& innerNodeUnsafe(Node node)
	{
		return *static_cast<InnerNode*>(node.data());
	}

	[[nodiscard]] InnerNode const& innerNode(Node node) const
	{
		return innerNodeUnsafe(operator()(node));
	}

	[[nodiscard]] InnerNode& innerNode(Node node)
	{
		return innerNodeUnsafe(operator()(node));
	}

	[[nodiscard]] InnerNode const& innerNode(Code code) const
	{
		assert(0 != code.depth());

		InnerNode const* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = code.depth();
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerChild(*node, index);
			--depth;
			index = code.index(depth);
		}

		return *node;
	}

	[[nodiscard]] InnerNode& innerNode(Code code)
	{
		assert(0 != code.depth());

		InnerNode* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = code.depth();
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerChild(*node, index);
			--depth;
			index = code.index(depth);
		}

		return *node;
	}

	[[nodiscard]] std::pair<LeafNode const&, depth_t> leafNodeAndDepth(Code code) const
	{
		InnerNode const* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerChild(*node, index);
			--depth;
			index = code.index(depth);
		}

		return 0 == code.depth() && !node->leaf[index]
		           ? std::pair<LeafNode const&, depth_t>(leafChild(*node, index), 0)
		           : std::pair<LeafNode const&, depth_t>(*node, depth);
	}

	[[nodiscard]] std::pair<LeafNode&, depth_t> leafNodeAndDepth(Code code)
	{
		InnerNode* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerChild(*node, index);
			--depth;
			index = code.index(depth);
		}

		return 0 == code.depth() && !node->leaf[index]
		           ? std::pair<LeafNode&, depth_t>(leafChild(*node, index), 0)
		           : std::pair<LeafNode&, depth_t>(*node, depth);
	}

	[[nodiscard]] std::pair<InnerNode const&, depth_t> innerNodeAndDepth(Code code) const
	{
		InnerNode const* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerChild(*node, index);
			--depth;
			auto index = code.index(depth);
		}

		return {*node, depth};
	}

	[[nodiscard]] std::pair<InnerNode&, depth_t> innerNodeAndDepth(Code code)
	{
		InnerNode* node = &root();
		depth_t depth = rootDepth();
		depth_t min_depth = std::max(code.depth(), depth_t(1));
		auto index = code.index(depth);
		while (min_depth != depth && !node->leaf[index]) {
			node = &innerChild(*node, index);
			--depth;
			auto index = code.index(depth);
		}

		return {*node, depth};
	}

	//
	// Get children
	//

	[[nodiscard]] static constexpr LeafNodeBlock const& leafChildren(InnerNode const& node)
	{
		return *node.leaf_children;
	}

	[[nodiscard]] static constexpr LeafNodeBlock& leafChildren(InnerNode& node)
	{
		return *node.leaf_children;
	}

	[[nodiscard]] static constexpr InnerNodeBlock const& innerChildren(
	    InnerNode const& node)
	{
		return *node.inner_children;
	}

	[[nodiscard]] static constexpr InnerNodeBlock& innerChildren(InnerNode& node)
	{
		return *node.inner_children;
	}

	[[nodiscard]] static constexpr LeafNode const& leafChild(InnerNode const& parent,
	                                                         index_t child_index)
	{
		return leafChildren(parent)[child_index];
	}

	[[nodiscard]] static constexpr LeafNode& leafChild(InnerNode& parent,
	                                                   index_t child_index)
	{
		return leafChildren(parent)[child_index];
	}

	[[nodiscard]] static constexpr InnerNode const& innerChild(InnerNode const& parent,
	                                                           index_t child_index)
	{
		return innerChildren(parent)[child_index];
	}

	[[nodiscard]] static constexpr InnerNode& innerChild(InnerNode& parent,
	                                                     index_t child_index)
	{
		return innerChildren(parent)[child_index];
	}

	[[nodiscard]] static constexpr LeafNode const& child(InnerNode const& parent,
	                                                     index_t child_index,
	                                                     depth_t parent_depth)
	{
		return 1 == parent_depth ? leafChild(parent, child_index)
		                         : innerChild(parent, child_index);
	}

	[[nodiscard]] static constexpr LeafNode& child(InnerNode& parent, index_t child_index,
	                                               depth_t parent_depth)
	{
		return 1 == parent_depth ? leafChild(parent, child_index)
		                         : innerChild(parent, child_index);
	}

	/**************************************************************************************
	|                                                                                     |
	|                                       Center                                        |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	//
	// Child center
	//

	[[nodiscard]] static constexpr Point childCenter(Point parent_center,
	                                                 node_size_t child_half_size,
	                                                 index_t child_index)
	{
		parent_center[0] += child_index & index_t(1) ? child_half_size : -child_half_size;
		parent_center[1] += child_index & index_t(2) ? child_half_size : -child_half_size;
		parent_center[2] += child_index & index_t(4) ? child_half_size : -child_half_size;
		return parent_center;
	}

	//
	// Sibling center
	//

	[[nodiscard]] static constexpr Point siblingCenter(Point center, node_size_t half_size,
	                                                   index_t index, index_t sibling_index)
	{
		index_t const temp = index ^ sibling_index;
		node_size_t const size = 2 * half_size;
		if (temp & index_t(1)) {
			center[0] += sibling_index & index_t(1) ? size : -size;
		}
		if (temp & index_t(2)) {
			center[1] += sibling_index & index_t(2) ? size : -size;
		}
		if (temp & index_t(4)) {
			center[2] += sibling_index & index_t(4) ? size : -size;
		}
		return center;
	}

	//
	// Parent center
	//

	[[nodiscard]] static constexpr Point parentCenter(Point child_center,
	                                                  node_size_t child_half_size,
	                                                  index_t child_index)
	{
		child_center[0] -= child_index & index_t(1) ? child_half_size : -child_half_size;
		child_center[1] -= child_index & index_t(2) ? child_half_size : -child_half_size;
		child_center[2] -= child_index & index_t(4) ? child_half_size : -child_half_size;
		return child_center;
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Leaf                                         |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Implement

	/**************************************************************************************
	|                                                                                     |
	|                                      Modified                                       |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Implement

	//
	// Set modified
	//

	void setModified(LeafNode& node, index_t index) { node.modified[index] = true; }

	void setModified(InnerNode& node, index_t index, depth_t depth, depth_t min_depth)
	{
		if (min_depth <= depth) {
			node.modified[index] = true;
		}

		if (min_depth < depth) {
			if (1 == depth) {
				leafChild(node, index).modified.set();
			} else {
				setModifiedRecurs(innerChild(node, index), depth - 1, min_depth);
			}
		}
	}

	void setModifiedRecurs(InnerNode& node, depth_t depth, depth_t min_depth)
	{
		node.modified.set();

		if (node.leaf.all() || depth == min_depth) {
			return;
		}

		if (1 == depth) {
			// All are allocated so we can just set all to modified, even if they are leaves
			for (auto& child : leafChildren(node)) {
				child.modified.set();
			}
		} else {
			for (index_t i = 0; 8 != i; ++i) {
				if (!node.leaf[i]) {
					setModifiedRecurs(innerNode(node, i), depth - 1, min_depth);
				}
			}
		}
	}

	//
	// Reset modified
	//

	void resetModified(LeafNode& node, index_t index) { node.modified[index] = false; }

	void resetModified(InnerNode& node, index_t index, depth_t depth, depth_t max_depth)
	{
		if (node.leaf[index] || !node.modified[index]) {
			if (depth <= max_depth) {
				node.modified[index] = false;
			}
			return;
		}

		if (1 == depth) {
			leafChild(node, index).modified.reset();
		} else {
			resetModifiedRecurs(innerChild(node, index), depth - 1, max_depth);
		}

		if (depth <= max_depth) {
			node.modified[index] = false;
		}
	}

	void resetModifiedRecurs(InnerNode& node, depth_t depth, depth_t max_depth)
	{
		IndexField modified_parents = node.modified & ~node.leaf;

		if (modified_parents.none()) {
			if (depth <= max_depth) {
				node.modified.reset();
			}
			return;
		}

		if (1 == depth) {
			// All are allocated so we can just reset all, even if they are leaves
			for (auto& child : leafChildren(node)) {
				child.modified.reset();
			}
		} else {
			for (index_t i = 0; 8 != i; ++i) {
				if (modified_parents[i]) {
					resetModifiedRecurs(innerChild(node, i), depth - 1, max_depth);
				}
			}
		}

		if (depth <= max_depth) {
			node.modified.reset();
		}
	}

	//
	// Set parents modified
	//

	void setModifiedParents(Code code)
	{
		setModifiedParentsRecurs(root(), rootDepth(), code);
	}

	// NOTE: Assumes code has depth higher then depth
	void setModifiedParentsRecurs(InnerNode& node, depth_t depth, Code code)
	{
		auto index = code.index(depth);
		node.modified[index] = true;
		if (code.depth() < depth - 1 && !std::as_const(node).leaf[index]) {
			setModifiedParentsRecurs(innerChild(node, index), depth - 1, code);
		}
	}

	/**************************************************************************************
	|                                                                                     |
	|                                      Propagate                                      |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	void updateNode(InnerNode& node, IndexField const indices, depth_t const depth)
	{
		if (1 == depth) {
			auto const& children = leafChildren(node);
			if (indices.all()) {
				for (index_t i = 0; 8 != i; ++i) {
					derived().updateNode(node, i, children[i]);
				}
			} else {
				for (index_t i = 0; 8 != i; ++i) {
					if (indices[i]) {
						derived().updateNode(node, i, children[i]);
					}
				}
			}
		} else {
			auto const& children = innerChildren(node);
			if (indices.all()) {
				for (index_t i = 0; 8 != i; ++i) {
					derived().updateNode(node, i, children[i]);
				}
			} else {
				for (index_t i = 0; 8 != i; ++i) {
					if (indices[i]) {
						derived().updateNode(node, i, children[i]);
					}
				}
			}
		}

		prune(node, indices, depth);
	}

	void propagateModified(LeafNode& node, index_t index, bool keep_modified)
	{
		if (!keep_modified) {
			node.modified[index] = false;
		}
	}

	void propagateModified(InnerNode& node, index_t index, depth_t depth,
	                       bool keep_modified, depth_t max_depth)
	{
		if (!std::as_const(node).modified[index]) {
			return;  // Nothing modified here
		}

		if (1 == depth) {
			if (!keep_modified) {
				leafChild(node, index).modified.reset();
			}
		} else {
			if (keep_modified) {
				propagateModifiedRecurs<true>(innerChild(node, index), depth - 1, max_depth);
			} else {
				propagateModifiedRecurs<false>(innerChild(node, index), depth - 1, max_depth);
			}
		}

		if (depth <= max_depth) {
			IndexField indices;
			indices[index] = true;
			updateNode(node, indices, depth);
			if (!keep_modified) {
				node.modified[index] = false;
			}
		}
	}

	template <bool KeepModified>
	void propagateModifiedRecurs(InnerNode& node, depth_t depth, depth_t max_depth)
	{
		IndexField const modified_parent = node.modified & ~node.leaf;

		if (modified_parent.none()) {
			if constexpr (!KeepModified) {
				if (depth <= max_depth) {
					node.modified.reset();
				}
			}
			return;
		}

		if (1 == depth) {
			if constexpr (!KeepModified) {
				for (std::size_t index = 0; 8 != index; ++index) {
					if (modified_parent[index]) {
						leafChild(node, index).modified.reset();
					}
				}
			}
		} else {
			for (std::size_t index = 0; 8 != index; ++index) {
				if (modified_parent[index]) {
					propagateModifiedRecurs<KeepModified>(innerChild(node, index), depth - 1,
					                                      max_depth);
				}
			}
		}

		if (depth <= max_depth) {
			updateNode(node, modified_parent, depth);
			if constexpr (!KeepModified) {
				node.modified.reset();
			}
		}
	}

	/**************************************************************************************
	|                                                                                     |
	|                                        Prune                                        |
	|                                                                                     |
	**************************************************************************************/

	// TODO: Add comments

	// If all children are the same as the parent they can be pruned
	// NOTE: Only call with nodes that have children
	[[nodiscard]] static IndexField isCollapsible(InnerNode const& node,
	                                              IndexField const indices,
	                                              depth_t const depth)
	{
		IndexField collapsible;
		for (index_t i = 0; 8 != i; ++i) {
			if (indices[i]) {
				if (1 == depth) {
					collapsible[i] = leafChild(node, i).isCollapsible();
				} else {
					collapsible[i] = innerChild(node, i).isCollapsible();
				}
			}
		}
		return collapsible;
	}

	// NOTE: Only call with nodes that have children
	IndexField prune(InnerNode& node, IndexField indices, depth_t depth)
	{
		indices = isCollapsible(node, indices, depth);
		if (indices.any()) {
			deleteChildren(node, indices, depth);
		}
		return indices;
	}

	IndexField pruneRecurs(InnerNode& node, depth_t depth)
	{
		if (node.leaf.all()) {
			return node.leaf;
		}

		if (1 == depth) {
			return node.leaf | prune(node, ~node.leaf, depth);
		}

		IndexField prunable;
		for (index_t i = 0; 8 != i; ++i) {
			if (!std::as_const(node).leaf[i]) {
				auto& child = innerChild(node, i);
				if ((~child.leaf) == pruneRecurs(child, depth - 1)) {
					prunable.set(i);
				}
			}
		}

		return node.isLeaf() | (prunable.none() ? prunable : prune(node, prunable, depth));
	}

	/**************************************************************************************
	|                                                                                     |
	|                                 Create/delete nodes                                 |
	|                                                                                     |
	**************************************************************************************/

	//
	// Create
	//

	inline void allocateLeafChildren(InnerNode& node)
	{
		if constexpr (ReuseNodes) {
			if constexpr (UFOLock::NONE == Lock) {
				if (!free_leaf_blocks_.empty()) {
					takeExistingLeafChildren(node);
					return;
				}
			} else if (lockIfNonEmptyLeaves()) {
				takeExistingLeafChildren(node);
				unlockLeaves();
				return;
			}
		}

		node.leaf_children = new LeafNodeBlock();
		if constexpr (CountNodes) {
			num_allocated_leaf_nodes_ += 8 * 8;
			num_allocated_inner_leaf_nodes_ -= 1 * 8;
			num_allocated_inner_nodes_ += 1 * 8;
		}
	}

	inline void allocateInnerChildren(InnerNode& node)
	{
		if constexpr (ReuseNodes) {
			if constexpr (UFOLock::NONE == Lock) {
				if (!free_inner_blocks_.empty()) {
					takeExistingInnerChildren(node);
					return;
				}
			} else if (lockIfNonEmptyInner()) {
				takeExistingInnerChildren(node);
				unlockInner();
				return;
			}
		}

		node.inner_children = new InnerNodeBlock();
		if constexpr (CountNodes) {
			// Got 8 * 8 new and 1 * 8 is made into a inner node
			num_allocated_inner_leaf_nodes_ += 7 * 8;
			num_allocated_inner_nodes_ += 1 * 8;
		}
	}

	inline void takeExistingLeafChildren(InnerNode& node)
	{
		node.leaf_children = free_leaf_blocks_.top();
		free_leaf_blocks_.pop();
	}

	inline void takeExistingInnerChildren(InnerNode& node)
	{
		node.inner_children = free_inner_blocks_.top();
		free_inner_blocks_.pop();
	}

	void createLeafChildren(InnerNode& node) { createLeafChildren(node, node.leaf); }

	void createLeafChildren(InnerNode& node, IndexField indices)
	{
		indices &= node.leaf;
		if (indices.none()) {
			return;
		}

		if constexpr (UFOLock::NONE != Lock) {
			if (!lockIfLeaf(node, indices, 0)) {
				return;
			}
		}

		if (!node.leaf_children) {
			allocateLeafChildren(node);
		}

		std::size_t num = 0;
		for (index_t i = 0; 8 != i; ++i) {
			if (std::as_const(indices)[i]) {
				++num;
				leafChild(node, i).fill(node, i);
			}
		}

		if constexpr (CountNodes) {
			num_leaf_nodes_ += 8 * num;
			num_inner_leaf_nodes_ -= 1 * num;
			num_inner_nodes_ += 1 * num;
		}

		node.leaf &= ~indices;
		if constexpr (UFOLock::NONE != Lock) {
			unlock(node, 0);
		}
	}

	void createLeafChildren(InnerNode& node, index_t index)
	{
		if constexpr (UFOLock::NONE != Lock) {
			if (!lockIfLeaf(node, index, 0)) {
				return;
			}
		}

		if (std::as_const(node).leaf[index]) {
			if (!node.leaf_children) {
				allocateLeafChildren(node);
			}

			leafChild(node, index).fill(node, index);
			if constexpr (CountNodes) {
				num_leaf_nodes_ += 8;
				num_inner_leaf_nodes_ -= 1;
				num_inner_nodes_ += 1;
			}
			node.leaf[index] = false;
		}

		if constexpr (UFOLock::NONE != Lock) {
			unlock(node, 0);
		}
	}

	void createInnerChildren(InnerNode& node, depth_t depth)
	{
		createInnerChildren(node, node.leaf, depth);
	}

	void createInnerChildren(InnerNode& node, IndexField indices, depth_t depth)
	{
		indices &= node.leaf;
		if (indices.none()) {
			return;
		}

		if constexpr (UFOLock::NONE != Lock) {
			if (!lockIfLeaf(node, indices, depth)) {
				return;
			}
		}

		if (!node.inner_children) {
			allocateInnerChildren(node);
		}

		std::size_t num = 0;
		for (index_t i = 0; 8 != i; ++i) {
			if (std::as_const(indices)[i]) {
				++num;
				innerChild(node, i).fill(node, i);
			}
		}

		if constexpr (CountNodes) {
			num_inner_leaf_nodes_ += 7 * num;
			num_inner_nodes_ += 1 * num;
		}

		node.leaf &= ~indices;
		if constexpr (UFOLock::DEPTH == Lock) {
			unlock(node, depth);
		}
	}

	void createInnerChildren(InnerNode& node, index_t index, depth_t depth)
	{
		if constexpr (UFOLock::DEPTH == Lock) {
			if (!lockIfLeaf(node, index, depth)) {
				return;
			}
		}

		if (std::as_const(node).leaf[index]) {
			if (!node.inner_children) {
				allocateInnerChildren(node);
			}

			innerChild(node, index).fill(node, index);
			if constexpr (CountNodes) {
				num_inner_leaf_nodes_ += 7;
				num_inner_nodes_ += 1;
			}
			node.leaf[index] = false;
		}

		if constexpr (UFOLock::DEPTH == Lock) {
			unlock(node, depth);
		}
	}

	//
	// Delete
	//

	inline void deallocateLeafChildren(InnerNode& node, bool prune)
	{
		if (prune) {
			delete &leafChildren(node);
			if constexpr (CountNodes) {
				num_allocated_leaf_nodes_ -= 8 * 8;
				num_allocated_inner_leaf_nodes_ += 1 * 8;
				num_allocated_inner_nodes_ -= 1 * 8;
			}
			node.leaf_children = nullptr;
		} else if constexpr (ReuseNodes) {
			if constexpr (UFOLock::NONE != Lock) {
				lockLeaves();
			}
			addExistingLeafChildren(node);
			if constexpr (UFOLock::NONE != Lock) {
				unlockLeaves();
			}
			node.leaf_children = nullptr;
		}
	}

	inline void deallocateInnerChildren(InnerNode& node, bool prune)
	{
		if (prune) {
			delete &innerChildren(node);
			if constexpr (CountNodes) {
				// Remove 8*8 and 1*8 inner node is made into a inner leaf node
				num_allocated_inner_leaf_nodes_ -= 7 * 8;
				num_allocated_inner_nodes_ -= 1 * 8;
			}
			node.inner_children = nullptr;
		} else if constexpr (ReuseNodes) {
			if constexpr (UFOLock::DEPTH == Lock) {
				lockInner();
			}
			addExistingInnerChildren(node);
			if constexpr (UFOLock::DEPTH == Lock) {
				unlockInner();
			}
			node.inner_children = nullptr;
		}
	}

	inline void addExistingLeafChildren(InnerNode& node)
	{
		free_leaf_blocks_.push(&leafChildren(node));
	}

	inline void addExistingInnerChildren(InnerNode& node)
	{
		free_inner_blocks_.push(*innerChildren(node));
	}

	void deleteLeafChildren(InnerNode& node, bool manual_pruning = false)
	{
		deleteLeafChildren(node, ~node.leaf, manual_pruning);
	}

	void deleteLeafChildren(InnerNode& node, IndexField indices,
	                        bool manual_pruning = false)
	{
		IndexField const new_leaf = indices & ~node.leaf;
		if (new_leaf.none()) {
			return;
		}

		node.leaf |= indices;

		std::size_t num = 0;
		for (index_t i = 0; 8 != i; ++i) {
			if (new_leaf[i]) {
				leafChild(node, i).clear();
				++num;
			}
		}

		if constexpr (CountNodes) {
			num_leaf_nodes_ -= 8 * num;
			num_inner_leaf_nodes_ += 1 * num;
			num_inner_nodes_ -= 1 * num;
		}

		if (node.leaf.all()) {
			// Can only remove if all nodes will be leaves
			deallocateLeafChildren(node, manual_pruning || automaticPruning());
		}
	}

	void deleteLeafChildren(InnerNode& node, index_t index, bool manual_pruning = false)
	{
		if (node.leaf[index]) {
			return;
		}

		node.leaf[index] = true;

		leafChild(node, index).clear();

		if constexpr (CountNodes) {
			num_leaf_nodes_ -= 8;
			num_inner_leaf_nodes_ += 1;
			num_inner_nodes_ -= 1;
		}

		if (node.leaf.all()) {
			// Can only remove if all nodes will be leaves
			deallocateLeafChildren(node, manual_pruning || automaticPruning());
		}
	}

	void deleteChildren(InnerNode& node, depth_t const depth, bool manual_pruning = false)
	{
		if (1 == depth) {
			deleteLeafChildren(node, manual_pruning);
		} else {
			deleteChildren(node, ~node.leaf, depth, manual_pruning);
		}
	}

	void deleteChildren(InnerNode& node, IndexField indices, depth_t const depth,
	                    bool manual_pruning = false)
	{
		if (1 == depth) {
			deleteLeafChildren(node, indices, manual_pruning);
			return;
		}

		IndexField const new_leaf = indices & ~node.leaf;
		if (new_leaf.none()) {
			return;
		}

		node.leaf |= indices;

		std::size_t num = 0;
		for (index_t i = 0; 8 != i; ++i) {
			if (new_leaf[i]) {
				InnerNode& child = innerChild(node, i);
				deleteChildren(child, depth - 1, manual_pruning);
				child.clear();
				++num;
			}
		}

		if constexpr (CountNodes) {
			num_inner_leaf_nodes_ -= 7 * num;
			num_inner_nodes_ -= 1 * num;
		}

		if (node.leaf.all()) {
			// Can only remove if all nodes will be leaves
			deallocateInnerChildren(node, manual_pruning || automaticPruning());
		}
	}

	void deleteChildren(InnerNode& node, index_t index, depth_t depth,
	                    bool manual_pruning = false)
	{
		if (1 == depth) {
			deleteLeafChildren(node, index, manual_pruning);
			return;
		}

		if (node.leaf[index]) {
			return;
		}

		node.leaf[index] = true;

		InnerNode& child = innerChild(node, index);
		deleteChildren(child, depth - 1, manual_pruning);
		child.clear();

		if constexpr (CountNodes) {
			num_inner_leaf_nodes_ -= 7;
			num_inner_nodes_ -= 1;
		}

		if (node.leaf.all()) {
			// Can only remove if all nodes will be leaves
			deallocateInnerChildren(node, manual_pruning || automaticPruning());
		}
	}

	//
	// (Un)lock children
	//

	[[nodiscard]] bool tryLockChildren(depth_t depth)
	{
		return !children_locks_[depth].test_and_set(std::memory_order_acquire);
	}

	[[nodiscard]] bool lockIfLeaf(InnerNode& node, index_t index, depth_t depth)
	{
		if constexpr (UFOLock::DEPTH == Lock) {
			do {
				if (!std::as_const(node).leaf[index]) {
					return false;
				}
			} while (children_locks_[depth].test_and_set(std::memory_order_acquire));
		} else if constexpr (UFOLock::NODE == Lock) {
			do {
				if (!std::as_const(node).leaf[index]) {
					return false;
				}
			} while (node.lock.test_and_set(std::memory_order_acquire));
		}

		if (!std::as_const(node).leaf[index]) {
			unlock(node, depth);
			return false;
		}

		return true;
	}

	[[nodiscard]] bool lockIfLeaf(InnerNode& node, IndexField const indices, depth_t depth)
	{
		if constexpr (UFOLock::DEPTH == Lock) {
			do {
				if ((node.leaf & indices).none()) {
					return false;
				}
			} while (children_locks_[depth].test_and_set(std::memory_order_acquire));
		} else {
			do {
				if ((node.leaf & indices).none()) {
					return false;
				}
			} while (node.lock.test_and_set(std::memory_order_acquire));
		}

		if ((node.leaf & indices).none()) {
			unlock(node, depth);
			return false;
		}

		return true;
	}

	void unlock(InnerNode& node, depth_t depth)
	{
		if constexpr (UFOLock::DEPTH == Lock) {
			children_locks_[depth].clear(std::memory_order_release);
		} else if (UFOLock::NODE == Lock) {
			node.lock.clear(std::memory_order_release);
		}
	}

	//
	// Leaf/inner lock
	//

	[[nodiscard]] bool tryLockLeaves()
	{
		return !free_leaf_block_lock_.test_and_set(std::memory_order_acquire);
	}

	void lockLeaves()
	{
		while (!tryLockLeaves()) {
		}
	}

	[[nodiscard]] bool lockIfNonEmptyLeaves()
	{
		do {
			if (free_leaf_blocks_.empty()) {
				return false;
			}
		} while (!tryLockLeaves());

		if (free_leaf_blocks_.empty()) {
			unlockLeaves();
			return false;
		}
		return true;
	}

	void unlockLeaves() { free_leaf_block_lock_.clear(std::memory_order_release); }

	[[nodiscard]] bool tryLockInner()
	{
		return !free_inner_block_lock_.test_and_set(std::memory_order_acquire);
	}

	void lockInner()
	{
		while (!tryLockInner()) {
		}
	}

	[[nodiscard]] bool lockIfNonEmptyInner()
	{
		do {
			if (free_inner_blocks_.empty()) {
				return false;
			}
		} while (!tryLockInner());

		if (free_inner_blocks_.empty()) {
			unlockInner();
			return false;
		}
		return true;
	}

	void unlockInner() { free_inner_block_lock_.clear(std::memory_order_release); }

	/**************************************************************************************
	|                                                                                     |
	|                                         I/O                                         |
	|                                                                                     |
	**************************************************************************************/

	struct NodeAndIndices {
		using node_type = LeafNode;

		node_type* node;
		IndexField indices;

		NodeAndIndices() = default;

		NodeAndIndices(LeafNode& node, IndexField indices) : node(&node), indices(indices) {}

		NodeAndIndices(NodeAndIndices const&) = default;

		NodeAndIndices(NodeAndIndices&&) = default;

		NodeAndIndices& operator=(NodeAndIndices const&) = default;

		NodeAndIndices& operator=(NodeAndIndices&&) = default;
	};

	[[nodiscard]] FileOptions fileOptions(bool const compress) const
	{
		FileOptions options;
		options.compressed = compress;
		options.leaf_size = size();
		options.depth_levels = depthLevels();
		return options;
	}

	[[nodiscard]] std::vector<NodeAndIndices> readNodes(std::istream& in)
	{
		auto tree = readTreeStructure(in);
		auto num_nodes = readNum(in);
		return retrieveNodes(tree, num_nodes);
	}

	[[nodiscard]] std::vector<NodeAndIndices> readNodes(ReadBuffer& in)
	{
		auto tree = readTreeStructure(in);
		auto num_nodes = readNum(in);
		return retrieveNodes(tree, num_nodes);
	}

	[[nodiscard]] std::unique_ptr<IndexField[]> readTreeStructure(std::istream& in)
	{
		std::uint64_t num = readNum(in);
		auto tree = std::make_unique<IndexField[]>(num);
		in.read(reinterpret_cast<char*>(tree.get()),
		        num * sizeof(typename decltype(tree)::element_type));
		return tree;
	}

	[[nodiscard]] std::unique_ptr<IndexField[]> readTreeStructure(ReadBuffer& in)
	{
		std::uint64_t num = readNum(in);
		auto tree = std::make_unique<IndexField[]>(num);
		in.read(tree.get(), num * sizeof(typename decltype(tree)::element_type));
		return tree;
	}

	[[nodiscard]] std::uint64_t readNum(std::istream& in)
	{
		std::uint64_t num;
		in.read(reinterpret_cast<char*>(&num), sizeof(num));
		return num;
	}

	[[nodiscard]] std::uint64_t readNum(ReadBuffer& in)
	{
		std::uint64_t num;
		in.read(&num, sizeof(num));
		return num;
	}

	[[nodiscard]] std::vector<NodeAndIndices> retrieveNodes(
	    std::unique_ptr<IndexField[]> const& tree, std::uint64_t num_nodes)
	{
		std::vector<NodeAndIndices> nodes;
		nodes.reserve(num_nodes);

		if (tree[0].any()) {
			nodes.emplace_back(root(), tree[0]);
			root().modified[rootIndex()] = true;
		} else if (tree[1].any()) {
			createInnerChildren(root(), rootIndex(), rootDepth());
			retrieveNodesRecurs(innerChildren(root()), tree[1], rootDepth() - 1, tree.get() + 2,
			                    nodes);
		}

		return nodes;
	}

	IndexField const* retrieveNodesRecurs(LeafNodeBlock& node, IndexField const indices,
	                                      IndexField const* tree,
	                                      std::vector<NodeAndIndices>& nodes)
	{
		for (index_t i = 0; 8 != i; ++i) {
			if (!indices[i]) {
				continue;
			}

			IndexField const valid_return = *tree++;
			nodes.emplace_back(node[i], valid_return);
			node[i].modified |= valid_return;
		}

		return tree;
	}

	IndexField const* retrieveNodesRecurs(InnerNodeBlock& node, IndexField const indices,
	                                      depth_t const depth, IndexField const* tree,
	                                      std::vector<NodeAndIndices>& nodes)
	{
		for (index_t i = 0; 8 != i; ++i) {
			if (!indices[i]) {
				continue;
			}

			IndexField const valid_return = *tree++;
			IndexField const valid_inner = *tree++;

			if (valid_return.any()) {
				nodes.emplace_back(node[i], valid_return);
			}

			if (valid_inner.any()) {
				if (1 == depth) {
					createLeafChildren(node[i], valid_inner);
					tree = retrieveNodesRecurs(leafChildren(node[i]), valid_inner, tree, nodes);
				} else {
					createInnerChildren(node[i], valid_inner, depth);
					tree = retrieveNodesRecurs(innerChildren(node[i]), valid_inner, depth - 1, tree,
					                           nodes);
				}
			}

			node[i].modified |= valid_return | valid_inner;
		}

		return tree;
	}

	template <class Predicates>
	[[nodiscard]] std::pair<std::vector<IndexField>, std::vector<LeafNode>> data(
	    Predicates const& predicates) const
	{
		std::vector<IndexField> tree;
		std::vector<LeafNode> nodes;

		std::conditional_t<predicate::contains_spatial_predicate_v<Predicates>, NodeBV, Node>
		    root_node;
		if constexpr (predicate::contains_spatial_predicate_v<Predicates>) {
			root_node = rootNodeBV();
		} else {
			root_node = rootNode();
		}

		bool valid_return = predicate::PredicateValueCheck<Predicates>::apply(
		    predicates, derived(), root_node);
		bool valid_inner = !valid_return && predicate::PredicateInnerCheck<Predicates>::apply(
		                                        predicates, derived(), root_node);

		tree.emplace_back(valid_return ? 1U : 0U);
		tree.emplace_back(valid_inner ? 1U : 0U);

		if (valid_return) {
			nodes.push_back(root());
		} else if (valid_inner) {
			dataRecurs(child(root_node, 0), predicates, tree, nodes);
			if (nodes.empty()) {
				tree.clear();
			}
		}

		return {std::move(tree), std::move(nodes)};
	}

	template <class Predicates, class NodeType>
	void dataRecurs(NodeType const& node, Predicates const& predicates,
	                std::vector<IndexField>& tree, std::vector<LeafNode>& nodes) const
	{
		IndexField valid_return;

		if (0 == node.depth()) {
			for (index_t i = 0; 8 != i; ++i) {
				if (predicate::PredicateValueCheck<Predicates>::apply(predicates, derived(),
				                                                      sibling(node, i))) {
					valid_return[i] = true;
				}
			}

			tree.push_back(valid_return);

			if (valid_return.any()) {
				nodes.emplace_back(leafNodeUnsafe(node));
			}
		} else {
			IndexField valid_inner;
			for (index_t i = 0; 8 != i; ++i) {
				auto s = sibling(node, i);

				if (predicate::PredicateValueCheck<Predicates>::apply(predicates, derived(), s)) {
					valid_return[i] = true;
				} else if (predicate::PredicateInnerCheck<Predicates>::apply(predicates,
				                                                             derived(), s)) {
					valid_inner[i] = true;
				}
			}

			tree.push_back(valid_return);
			tree.push_back(valid_inner);

			auto cur_tree_size = tree.size();
			auto cur_nodes_size = nodes.size();

			if (valid_return.any()) {
				nodes.emplace_back(leafNodeUnsafe(node));
			}

			if (valid_inner.any()) {
				for (index_t i = 0; 8 != i; ++i) {
					if (valid_inner[i]) {
						auto s = sibling(node, i);
						dataRecurs(child(s, 0), predicates, tree, nodes);
					}
				}
			}

			if (nodes.size() == cur_nodes_size) {
				tree.resize(cur_tree_size);
				tree[tree.size() - 1] = 0;
				tree[tree.size() - 2] = 0;
			}
		}
	}

	template <bool Propagate>
	[[nodiscard]] void modifiedData()
	{
		modified_tree_.clear();
		modified_nodes_.clear();

		depth_t depth = rootDepth();

		bool const valid_return = std::as_const(root()).leaf[rootIndex()] &&
		                          std::as_const(root()).modified[rootIndex()];
		bool const valid_inner = !std::as_const(root()).leaf[rootIndex()] &&
		                         std::as_const(root()).modified[rootIndex()];

		modified_tree_.push_back(valid_return);
		modified_tree_.push_back(valid_inner);

		if (valid_return) {
			modified_nodes_.emplace_back(root());
			if constexpr (Propagate) {
				propagate(root(), valid_return);
			}
		} else if (valid_inner) {
			modifiedDataRecurs<Propagate>(innerChildren(root()), IndexField(1), depth - 1);
			if constexpr (Propagate) {
				propagate(root(), valid_inner, depth);
			}
			if (modified_nodes_.empty()) {
				modified_tree_.clear();
			}
		}

		root().modified.reset();
	}

	template <bool Propagate>
	void modifiedDataRecurs(LeafNodeBlock& node_block, IndexField const indices)
	{
		for (index_t i = 0; node_block.size() != i; ++i) {
			if (!indices[i]) {
				continue;
			}

			IndexField const m = node_block[i].modified;
			modified_tree_.push_back(m);

			if (m.none()) {
				continue;
			}

			modified_nodes_.emplace_back(node_block[i]);

			if constexpr (Propagate) {
				propagate(node_block[i], m);
			}

			node_block[i].modified.reset();
		}
	}

	template <bool Propagate>
	void modifiedDataRecurs(InnerNodeBlock& node_block, IndexField const indices,
	                        depth_t const depth)
	{
		for (index_t i = 0; node_block.size() != i; ++i) {
			if (!indices[i]) {
				continue;
			}

			IndexField const m = node_block[i].modified;
			IndexField const l = node_block[i].leaf;

			IndexField const valid_return = m & l;
			IndexField const valid_inner = m & ~l;

			modified_tree_.push_back(valid_return);
			modified_tree_.push_back(valid_inner);

			auto const cur_tree_size = modified_tree_.size();
			auto const cur_nodes_size = modified_nodes_.size();

			if (valid_return.any()) {
				modified_nodes_.emplace_back(node_block[i]);
			}

			if (valid_inner.any()) {
				if (1 == depth) {
					modifiedDataRecurs<Propagate>(leafChildren(node_block[i]), valid_inner);
				} else {
					modifiedDataRecurs<Propagate>(innerChildren(node_block[i]), valid_inner,
					                              depth - 1);
				}
			}

			if constexpr (Propagate) {
				propagate(node_block[i], m);
			}

			node_block[i].modified.reset();

			if (modified_nodes_.size() == cur_nodes_size) {
				modified_tree_.resize(cur_tree_size);
				modified_tree_[modified_tree_.size() - 1] = 0;
				modified_tree_[modified_tree_.size() - 2] = 0;
			}
		}
	}

	void write(std::ostream& out, std::vector<IndexField> const& tree,
	           std::vector<LeafNode> const& nodes, bool compress,
	           int compression_acceleration_level, int compression_level) const
	{
		writeHeader(out, fileOptions(compress));
		writeTreeStructure(out, tree);
		writeNumNodes(out, nodes.size());
		writeNodes(out, std::cbegin(nodes), std::cend(nodes), compress,
		           compression_acceleration_level, compression_level);
	}

	void write(WriteBuffer& out, std::vector<IndexField> const& tree,
	           std::vector<LeafNode> const& nodes, bool compress,
	           int compression_acceleration_level, int compression_level) const
	{
		writeHeader(out, fileOptions(compress));
		writeTreeStructure(out, tree);
		writeNumNodes(out, nodes.size());
		writeNodes(out, std::cbegin(nodes), std::cend(nodes), compress,
		           compression_acceleration_level, compression_level);
	}

	void writeTreeStructure(std::ostream& out, std::vector<IndexField> const& tree) const
	{
		std::uint64_t num = tree.size();
		out.write(reinterpret_cast<char const*>(&num), sizeof(num));
		out.write(reinterpret_cast<char const*>(tree.data()),
		          num * sizeof(typename std::decay_t<decltype(tree)>::value_type));
	}

	void writeTreeStructure(WriteBuffer& out, std::vector<IndexField> const& tree) const
	{
		std::uint64_t num = tree.size();
		out.write(&num, sizeof(num));
		out.write(tree.data(),
		          num * sizeof(typename std::decay_t<decltype(tree)>::value_type));
	}

	void writeNumNodes(std::ostream& out, std::uint64_t num_nodes) const
	{
		out.write(reinterpret_cast<char const*>(&num_nodes), sizeof(num_nodes));
	}

	void writeNumNodes(WriteBuffer& out, std::uint64_t num_nodes) const
	{
		out.write(&num_nodes, sizeof(num_nodes));
	}

	template <class InputIt>
	void writeNodes(std::ostream& out, InputIt first, InputIt last, bool const compress,
	                int const compression_acceleration_level,
	                int const compression_level) const
	{
		derived().writeNodes(out, first, last, compress, compression_acceleration_level,
		                     compression_level);
	}

	template <class InputIt>
	void writeNodes(WriteBuffer& out, InputIt first, InputIt last, bool const compress,
	                int const compression_acceleration_level,
	                int const compression_level) const
	{
		derived().writeNodes(out, first, last, false, compress,
		                     compression_acceleration_level, compression_level);
	}

 protected:
	// The number of depth levels
	depth_t depth_levels_;
	// The maximum coordinate value the octree can store
	key_t max_value_;

	// The root of the octree
	InnerNode root_;

	// Stores the node size at a given depth, where the depth is the index
	std::array<node_size_t, maxDepthLevels()> node_size_;
	// Reciprocal of the node size at a given depth, where the depth is the index
	std::array<node_size_t, maxDepthLevels()> node_size_factor_;

	// Automatic pruning
	bool automatic_prune_ = true;

	// Locks to support parallel insertion, one per depth level
	std::array<std::atomic_flag, maxDepthLevels()> children_locks_;

	// Free inner node blocks that can be used instead of allocating new
	std::stack<InnerNodeBlock*> free_inner_blocks_;
	// Free leaf node blocks that can be used instead of allocating new
	std::stack<LeafNodeBlock*> free_leaf_blocks_;

	// Lock for accessing `free_inner_blocks_`
	std::atomic_flag free_inner_block_lock_ = ATOMIC_FLAG_INIT;
	// Lock for accessing `free_leaf_blocks_`
	std::atomic_flag free_leaf_block_lock_ = ATOMIC_FLAG_INIT;

	//
	// Memory
	//

	// Current number of inner nodes
	std::atomic_size_t num_inner_nodes_ = 0;
	// Current number of inner leaf nodes
	std::atomic_size_t num_inner_leaf_nodes_ = 1;
	// Current number of leaf nodes
	std::atomic_size_t num_leaf_nodes_ = 0;

	// Current number of allocated inner nodes
	std::atomic_size_t num_allocated_inner_nodes_ = 0;
	// Current number of allocated inner leaf nodes
	std::atomic_size_t num_allocated_inner_leaf_nodes_ = 1 * 8;
	// Current number of allocated leaf nodes
	std::atomic_size_t num_allocated_leaf_nodes_ = 0;

	//
	// Store for performance
	//
	std::vector<IndexField> modified_tree_;
	std::vector<LeafNode> modified_nodes_;
};

}  // namespace ufo::map

#endif  // UFO_MAP_OCTREE_BASE_H