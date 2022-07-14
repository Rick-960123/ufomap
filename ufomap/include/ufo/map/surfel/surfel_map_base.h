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

#ifndef UFO_MAP_SURFEL_MAP_BASE_H
#define UFO_MAP_SURFEL_MAP_BASE_H

// UFO
#include <ufo/algorithm/algorithm.h>
#include <ufo/map/code/code_unordered_map.h>
#include <ufo/map/predicate/surfel.h>
// #include <ufo/map/types.h>

// STL
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace ufo::map
{
template <class Derived, class LeafNode, class InnerNode>
class SurfelMapBase
{
 public:
	using Surfel = typename LeafNode::surfel_type;

 public:
	//
	// Get surfel
	//

	std::optional<Surfel> getSurfel(Node node) const
	{
		return getSurfel(derived().getLeafNode(node));
	}

	std::optional<Surfel> getSurfel(Code code) const
	{
		return getSurfel(derived().getLeafNode(code));
	}

	std::optional<Surfel> getSurfel(Key key) const
	{
		return getTimeStep(Derived::toCode(key));
	}

	std::optional<Surfel> getSurfel(Point3 coord, depth_t depth = 0) const
	{
		return getTimeStep(derived().toCode(coord, depth));
	}

	std::optional<Surfel> getSurfel(coord_t x, coord_t y, coord_t z,
	                                depth_t depth = 0) const
	{
		return getTimeStep(derived().toCode(x, y, z, depth));
	}

	//
	// Get number of surfel points
	//

	std::size_t getNumSurfelPoints(Node node) const
	{
		if (auto surfel = getSurfel(node)) {
			return surfel->numPoints();
		} else {
			return 0;
		}
	}

	std::size_t getNumSurfelPoints(Code code) const
	{
		if (auto surfel = getSurfel(code)) {
			return surfel->numPoints();
		} else {
			return 0;
		}
	}

	std::size_t getNumSurfelPoints(Key key) const
	{
		return getNumSurfelPoints(derived().toCode(key));
	}

	std::size_t getNumSurfelPoints(Point3 coord, depth_t depth = 0) const
	{
		return getNumSurfelPoints(derived().toCode(coord, depth));
	}

	std::size_t getNumSurfelPoints(coord_t x, coord_t y, coord_t z, depth_t depth = 0) const
	{
		return getNumSurfelPoints(derived().toCode(x, y, z, depth));
	}

	//
	// Set surfel
	//

	void setSurfel(Node node, Surfel const& surfel, bool propagate = true)
	{
		derived().apply(
		    node, [this, &surfel](auto&& node) { setSurfel(node, surfel); }, propagate);
	}

	void setSurfel(Code code, Surfel const& surfel, bool propagate = true)
	{
		derived().apply(
		    code, [this, &surfel](auto&& node) { setSurfel(node, surfel); }, propagate);
	}

	void setSurfel(Key key, Surfel const& surfel, bool propagate = true)
	{
		setSurfel(derived().toCode(key), surfel, propagate);
	}

	void setSurfel(Point3 coord, Surfel const& surfel, depth_t depth = 0,
	               bool propagate = true)
	{
		setSurfel(derived().toCode(coord, depth), surfel, propagate);
	}

	void setSurfel(coord_t x, coord_t y, coord_t z, Surfel const& surfel, depth_t depth = 0,
	               bool propagate = true)
	{
		setSurfel(derived().toCode(x, y, z, depth), surfel, propagate);
	}

	//
	// TODO: Insert surfel
	//

	//
	// TODO: Remove surfel
	//

	//
	// Insert surfel point
	//

	void insertSurfelPoint(Point3 point, bool propagate = true)
	{
		derived().apply(
		    derived().toCode(point),
		    [this, point](auto&& node) { insertSurfelPoint(node, point); }, propagate);
	}

	template <class InputIt>
	void insertSurfelPoint(InputIt first, InputIt last, bool propagate = true)
	{
		CodeUnorderedMap<std::vector<Point3>> codes;
		std::for_each(first, last, [this, &codes](auto&& point) {
			codes[derived().toCode(point)].push_back(point);
		});

		for (auto const& elem : codes) {
			derived().apply(
			    elem.first,
			    [this, &elem](auto&& node) {
				    insertSurfelPoint(node, std::begin(elem.second), std::end(elem.second));
			    },
			    false);
		}

		if (propagate) {
			derived().updateModifiedNodes();
		}
	}

	void insertSurfelPoint(std::initializer_list<Point3> points, bool propagate = true)
	{
		insertSurfelPoint(std::begin(points), std::end(points), propagate);
	}

	//
	// Erase surfel point
	//

	void eraseSurfelPoint(Point3 point, bool propagate = true)
	{
		derived().apply(
		    derived().toCode(point),
		    [this, point](auto&& node) { eraseSurfelPoint(node, point); }, propagate);
	}

	template <class InputIt>
	void eraseSurfelPoint(InputIt first, InputIt last, bool propagate = true)
	{
		CodeUnorderedMap<std::vector<Point3>> codes;
		std::for_each(first, last, [this, &codes](auto&& point) {
			codes[derived().toCode(point)].push_back(point);
		});

		for (auto const& elem : codes) {
			derived().apply(
			    elem.first,
			    [this, &elem](auto&& node) {
				    eraseSurfelPoint(node, std::begin(elem.second), std::end(elem.second));
			    },
			    false);
		}

		if (propagate) {
			derived().updateModifiedNodes();
		}
	}

	void eraseSurfelPoint(std::initializer_list<Point3> points, bool propagate = true)
	{
		eraseSurfelPoint(std::begin(points), std::end(points), propagate);
	}

 protected:
	//
	// Derived
	//

	constexpr Derived& derived() { return *static_cast<Derived*>(this); }

	constexpr Derived const& derived() const { return *static_cast<Derived const*>(this); }

	//
	// Initilize root
	//

	void initRoot() { clearSurfel(derived().getRoot()); }

	//
	// Get surfel
	//

	static constexpr std::optional<std::reference_wrapper<Surfel>> getSurfel(LeafNode& node)
	{
		return node.surfel ? std::optional<std::reference_wrapper<Surfel>>{*node.surfel}
		                   : std::nullopt;
	}

	static constexpr std::optional<std::reference_wrapper<Surfel const>> getSurfel(
	    LeafNode const& node)
	{
		return node.surfel ? std::optional<std::reference_wrapper<Surfel const>>{*node.surfel}
		                   : std::nullopt;
	}

	//
	// Set surfel
	//

	static constexpr void setSurfel(LeafNode& node, Surfel const& surfel) noexcept
	{
		node.surfel = std::make_unique<Surfel>(surfel);
	}

	template <class... Args>
	static constexpr void setSurfel(LeafNode& node, Args&&... args)
	{
		node.surfel = std::make_unique<Surfel>(std::forward<Args>(args)...);
	}

	//
	// Clear surfel
	//

	static constexpr void clearSurfel(LeafNode& node) noexcept { node.surfel.reset(); }

	//
	// Has surfel
	//

	static constexpr bool hasSurfel(LeafNode const& node) noexcept
	{
		return nullptr != node.surfel;
	}

	void insertSurfel(LeafNode& node, Surfel const& surfel)
	{
		if (hasSurfel(node)) {
			node.surfel->addSurfel(surfel);
		} else {
			setSurfel(node, surfel);
		}
	}

	template <class InputIt>
	void insertSurfel(LeafNode& node, InputIt first, InputIt last)
	{
		if (first == last) {
			return;
		}

		if (!hasSurfel(node)) {
			setSurfel(node, *first);
			++first;
		}

		for (; first != last; ++first) {
			node.surfel->addSurfel(*first);
		}
	}

	//
	// Erase surfel
	//

	void eraseSurfel(LeafNode& node, Surfel const& surfel)
	{
		if (!hasSurfel(node)) {
			return;
		}

		if (node.surfel->numPoints() <= surfel.numPoints()) {
			clearSurfel();

		} else {
			node.surfel->removeSurfel(surfel);
		}
	}

	//
	// Insert surfel point
	//

	void insertSurfelPoint(LeafNode& node, Point3 point)
	{
		if (hasSurfel(node)) {
			node.surfel->addPoint(point);
		} else {
			setSurfel(node, point);
		}
	}

	template <class InputIt>
	void insertSurfelPoint(LeafNode& node, InputIt first, InputIt last)
	{
		if (hasSurfel(node)) {
			node.surfel->addPoint(first, last);
		} else {
			setSurfel(node, first, last);
		}
	}

	//
	// Erase surfel point
	//

	void eraseSurfelPoint(LeafNode& node, Point3 point)
	{
		if (hasSurfel(node)) {
			if (1 == node.surfel->num_points) {
				clearSurfel(node);
			} else {
				node.surfel->removePoint(point);
			}
		}
	}

	template <class InputIt>
	void eraseSurfelPoint(LeafNode& node, InputIt first, InputIt last)
	{
		if (hasSurfel(node)) {
			if (node.surfel->numPoints() <= std::distance(first, last)) {
				clearSurfel(node);
			} else {
				node.surfel->removePoint(first, last);
			}
		}
	}

	//
	// Update node
	//

	void updateNode(InnerNode& node, depth_t depth)
	{
		std::vector<Surfel> surfels;
		if (1 == depth) {
			for (LeafNode const& child : derived().getLeafChildren(node)) {
				if (hasSurfel(child)) {
					surfels.push_back(*child.surfel);
				}
			}
		} else {
			for (InnerNode const& child : derived().getInnerChildren(node)) {
				if (hasSurfel(child)) {
					surfels.push_back(*child.surfel);
				}
			}
		}

		if (surfels.empty()) {
			clearSurfel(node);
		} else {
			insertSurfel(node, std::begin(surfels), std::end(surfels));
		}
	}

	//
	// FIXME: REMOVE Update node indicators
	//

	void updateNodeIndicators(LeafNode&) {}

	void updateNodeIndicators(InnerNode&, depth_t) {}

	//
	// Input/output (read/write)
	//

	void addFileInfo(FileInfo& info) const
	{
		info["fields"].emplace_back("surfel");
		info["type"].emplace_back("U");
		info["size"].emplace_back(std::to_string(sizeof(Surfel)));
	}

	bool readNodes(std::istream& in_stream, std::vector<LeafNode*> const& nodes,
	               std::string const& field, char type, uint64_t size, uint64_t num)
	{
		if ("surfel" != field) {
			return false;
		}

		if ('U' == type && sizeof(Surfel) == size) {
			auto data = std::make_unique<Surfel[]>(nodes.size());
			in_stream.read(reinterpret_cast<char*>(data.get()),
			               nodes.size() * sizeof(uintptr_t));

			for (size_t i = 0; i != nodes.size(); ++i) {
				setSurfel(*nodes[i], reinterpret_cast<Surfel*>(data[i]));
			}
		} else {
			return false;
		}
		return true;
	}

	void writeNodes(std::ostream& out_stream, std::vector<LeafNode> const& nodes,
	                bool compress, int compression_acceleration_level,
	                int compression_level) const
	{
		uint64_t const size = nodes.size();
		out_stream.write(reinterpret_cast<char const*>(&size), sizeof(size));

		auto data = std::make_unique<uintptr_t[]>(nodes.size());
		for (size_t i = 0; i != nodes.size(); ++i) {
			data[i] = reinterpret_cast<uintptr_t>(getSurfel(nodes[i]));
		}

		out_stream.write(reinterpret_cast<char const*>(data.get()),
		                 nodes.size() * sizeof(uintptr_t));
	}

 protected:
	//  Surfel depth
	depth_t surfel_depth_ = 0;
};
}  // namespace ufo::map

#endif  // UFO_MAP_SURFEL_MAP_BASE_H