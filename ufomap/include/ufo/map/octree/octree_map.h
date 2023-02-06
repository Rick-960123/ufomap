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

#ifndef UFO_MAP_H
#define UFO_MAP_H

// UFO
#include <ufo/algorithm/algorithm.h>
#include <ufo/map/octree/octree.h>

// STL
#include <future>

namespace ufo::map
{
// NOTE: Remove this when it is possible to friend varidic number of classes
#define REPEAT_2(M, N) M(N) M(N + 1)
#define REPEAT_4(M, N) REPEAT_2(M, N) REPEAT_2(M, N + 2)
#define REPEAT_8(M, N) REPEAT_4(M, N) REPEAT_4(M, N + 4)
#define REPEAT_16(M, N) REPEAT_8(M, N) REPEAT_8(M, N + 8)
#define REPEAT_32(M, N) REPEAT_16(M, N) REPEAT_16(M, N + 16)
#define REPEAT_64(M, N) REPEAT_32(M, N) REPEAT_32(M, N + 32)
#define REPEAT_128(M, N) REPEAT_64(M, N) REPEAT_64(M, N + 64)

// All your base are belong to us
template <template <class, std::size_t> class... Maps>
class OctreeMap : public Octree<OctreeMap<Maps...>>, public Maps<OctreeMap<Maps...>, 8>...
{
 protected:
	//
	// Friends
	//

	friend Octree<OctreeMap>;
#define FRIEND(N)                                                       \
	friend std::tuple_element_t<std::min(static_cast<std::size_t>(N + 1), \
	                                     sizeof...(Maps)),                \
	                            std::tuple<void, Maps<OctreeMap, 8>...>>;
	REPEAT_128(FRIEND, 0)

 public:
	//
	// Constructors
	//

	OctreeMap(node_size_t leaf_node_size = 0.1, depth_t depth_levels = 17)
	    : Octree<OctreeMap>(leaf_node_size, depth_levels)
	{
		initRoot();
	}

	OctreeMap(std::filesystem::path const& file) : OctreeMap(0.1, 17)
	{
		Octree<OctreeMap>::read(file);
	}

	OctreeMap(std::istream& in) : OctreeMap(0.1, 17) { Octree<OctreeMap>::read(in); }

	OctreeMap(ReadBuffer& in) : OctreeMap(0.1, 17) { Octree<OctreeMap>::read(in); }

	OctreeMap(OctreeMap const& other)
	    : Octree<OctreeMap>(other), Maps<OctreeMap, 8>(other)...
	{
	}

	template <template <class> class... Maps2>
	OctreeMap(OctreeMap<Maps2...> const& other)
	    : Octree<OctreeMap>(other), Maps<OctreeMap, 8>(other)...
	{
		(Maps<OctreeMap, 8>::resize(Octree<OctreeMap>::size()), ...);  // TODO: Implement
	}

	OctreeMap(OctreeMap&& other) = default;

	OctreeMap& operator=(OctreeMap const& rhs)
	{
		Octree<OctreeMap>::operator=(rhs);
		(Maps<OctreeMap, 8>::operator=(rhs), ...);
		return *this;
	}

	template <template <class> class... Maps2>
	OctreeMap& operator=(OctreeMap<Maps2...> const& rhs)
	{
		Octree<OctreeMap>::operator=(rhs);
		(Maps<OctreeMap, 8>::operator=(rhs), ...);
		(Maps<OctreeMap, 8>::resize(Octree<OctreeMap>::size()), ...);  // TODO: Implement
		return *this;
	}

	OctreeMap& operator=(OctreeMap&& rhs) = default;

	//
	// Swap
	//

	void swap(OctreeMap& other)  // TODO: Add noexcept thing
	{
		Octree<OctreeMap>::swap(other);
		(Maps<OctreeMap, 8>::swap(other), ...);
	}

 protected:
	//
	// Allocate node block
	//

	void allocateNodeBlock() { (Maps<OctreeMap, 8>::allocateNodeBlock(), ...); }

	//
	// Apply Permutation
	//

	void applyPermutation(Permutation const& perm)
	{
		(Maps<OctreeMap, 8>::applyPermutation(perm), ...);
	}

	//
	// Initialize root
	//

	void initRoot()
	{
		Octree<OctreeMap>::allocateNodeBlock();
		Octree<OctreeMap>::initRoot();
		(Maps<OctreeMap, 8>::allocateNodeBlock(), ...);
		(Maps<OctreeMap, 8>::initRoot(), ...);
	}

	//
	// Fill
	//

	void fill(Index node, index_t children)
	{
		(Maps<OctreeMap, 8>::fill(node, children), ...);
	}

	//
	// Clear
	//

	void clear() { (Maps<OctreeMap, 8>::clear(), ...); }

	void clear(index_t nodes) { (Maps<OctreeMap, 8>::clear(nodes), ...); }

	//
	// Shrink to fit
	//

	void shrinkToFit() { (Maps<OctreeMap, 8>::shrinkToFit(), ...); }

	//
	// Update node
	//

	void updateNode(Index idx, index_t children_index)
	{
		(Maps<OctreeMap, 8>::updateNode(idx, children_index), ...);
	}

	//
	// Is prunable
	//

	[[nodiscard]] bool isPrunable(index_t index) const
	{
		return (Maps<OctreeMap, 8>::isPrunable(index) && ...);
	}

	//
	// Memory node block
	//

	[[nodiscard]] static constexpr std::size_t memoryNodeBlock() const noexcept
	{
		return (Maps<OctreeMap, 8>::memoryNodeBlock() + ...);
	}

	//
	// Input/output (read/write)
	//

	[[nodiscard]] static constexpr mt_t mapType() noexcept
	{
		return (Maps<OctreeMap, 8>::mapType() | ...);
	}

	template <class InputIt>
	[[nodiscard]] std::size_t serializedSize(InputIt first, InputIt last, bool compress,
	                                         mt_t data) const
	{
		return (serializedSize<Maps<OctreeMap, 8>>(first, last, compress, data) + ...);
	}

	template <class OutputIt>
	void readNodes(std::istream& in, OutputIt first, OutputIt last, bool const compressed,
	               mt_t const map_types)
	{
		auto cur_pos = in.tellg();
		in.seekg(0, std::ios_base::end);
		auto end_pos = in.tellg();
		in.seekg(cur_pos);

		Buffer buf;
		Buffer compress_buf;
		while (in.tellg() != end_pos && in.good()) {
			MapType mt;
			std::uint64_t data_size;

			in.read(reinterpret_cast<char*>(&mt), sizeof(mt));
			in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

			if ((mt_t{0} == map_types || mt_t{0} != (mt & map_types)) &&
			    (Maps<OctreeMap, 8>::canReadData(mt) || ...)) {
				(readNodes<Maps<OctreeMap, 8>>(in, buf, compress_buf, first, last, mt, data_size,
				                               compressed) ||
				 ...);
			} else {
				// Skip forward
				in.seekg(data_size, std::istream::cur);
			}
		}
	}

	template <class OutputIt>
	void readNodes(ReadBuffer& in, OutputIt first, OutputIt last, bool const compressed,
	               mt_t const map_types)
	{
		// std::vector<std::future<void>> res;

		Buffer compress_buf;
		while (in.readIndex() < in.size()) {
			std::cout << "Start: " << in.readIndex() << " vs " << in.size() << '\n';
			MapType mt;
			std::uint64_t data_size;

			in.read(&mt, sizeof(mt));
			in.read(&data_size, sizeof(data_size));

			std::cout << "Data size " << data_size << '\n';
			std::cout << "Map type " << mt << '\n';

			std::uint64_t next_index = in.readIndex() + data_size;

			if (mt_t{0} == map_types || mt_t{0} != (mt & map_types)) {
				(readNodes<Maps<OctreeMap, 8>>(in, compress_buf, first, last, mt, data_size,
				                               compressed) ||
				 ...);
			}

			// Skip forward
			in.setReadIndex(next_index);
			std::cout << "End: " << in.readIndex() << " vs " << in.size() << '\n';
		}

		// for (auto const& r : res) {
		// 	r.wait();
		// }
	}

	template <class InputIt>
	void writeNodes(std::ostream& out, InputIt first, InputIt last, bool const compress,
	                mt_t const map_types, int const compression_acceleration_level,
	                int const compression_level) const
	{
		Buffer buf;
		(writeNodes<Maps<OctreeMap, 8>>(out, buf, first, last, compress, map_types,
		                                compression_acceleration_level, compression_level),
		 ...);
	}

	template <class InputIt>
	void writeNodes(WriteBuffer& out, InputIt first, InputIt last, bool const compress,
	                mt_t const map_types, int const compression_acceleration_level,
	                int const compression_level) const
	{
		out.reserve(out.size() + serializedSize(first, last, compress, map_types));
		(writeNodes<Maps<OctreeMap, 8>>(out, first, last, compress, map_types,
		                                compression_acceleration_level, compression_level),
		 ...);
	}

 private:
	//
	// Input/output (read/write)
	//

	template <class Map, class InputIt>
	std::size_t serializedSize(InputIt first, InputIt last, bool compress,
	                           mt_t map_types) const
	{
		if (mt_t{0} != map_types && mt_t{0} == (Map::mapType() & map_types)) {
			return 0;
		}

		if (compress) {
			return sizeof(MapType) + sizeof(std::uint64_t) + sizeof(std::uint64_t) +
			       maxSizeCompressed(Map::serializedSize(first, last));
		} else {
			return sizeof(MapType) + sizeof(std::uint64_t) + Map::serializedSize(first, last);
		}
	}

	template <class Map, class OutputIt>
	bool readNodes(std::istream& in, Buffer& buf, Buffer& compress_buf, OutputIt first,
	               OutputIt last, MapType const mt, uint64_t const data_size,
	               bool const compressed)
	{
		if (!Map::canReadData(mt)) {
			return false;
		}

		buf.clear();
		// TODO: Implement better (should probably be resize?)
		buf.reserve(data_size);
		in.read(reinterpret_cast<char*>(buf.data()), data_size);
		return readNodes(buf, compress_buf, first, last, mt, data_size, compressed);
	}

	template <class Map, class OutputIt>
	bool readNodes(ReadBuffer& in, Buffer& compress_buf, OutputIt first, OutputIt last,
	               MapType const mt, uint64_t const data_size, bool const compressed)
	{
		if (!Map::canReadData(mt)) {
			return false;
		}

		if (compressed) {
			compress_buf.clear();

			std::uint64_t uncompressed_size;
			in.read(&uncompressed_size, sizeof(uncompressed_size));

			decompressData(in, compress_buf, uncompressed_size);

			Map::readNodes(compress_buf, first, last);
		} else {
			Map::readNodes(in, first, last);
		}

		return true;
	}

	template <class Map, class InputIt>
	void writeNodes(std::ostream& out, Buffer& buf, InputIt first, InputIt last,
	                bool const compress, mt_t const map_types,
	                int const compression_acceleration_level,
	                int const compression_level) const
	{
		if (mt_t{0} != map_types && mt_t{0} == (Map::mapType() & map_types)) {
			return;
		}

		buf.clear();
		writeNodes(buf, first, last, compress, compression_acceleration_level,
		           compression_level);

		if (!buf.empty()) {
			out.write(reinterpret_cast<char const*>(buf.data()), buf.size());
		}
	}

	template <class Map, class InputIt>
	void writeNodes(WriteBuffer& out, InputIt first, InputIt last, bool const compress,
	                mt_t const map_types, int const compression_acceleration_level,
	                int const compression_level) const
	{
		constexpr MapType mt = Map::mapType();
		if constexpr (MapType::NONE == mt) {
			return;
		}

		if (mt_t{0} != map_types && mt_t{0} == (Map::mapType() & map_types)) {
			return;
		}

		out.write(&mt, sizeof(mt));

		std::uint64_t size;
		auto size_index = out.writeIndex();
		out.setWriteIndex(size_index + sizeof(size));

		if (compress) {
			Buffer data;
			data.reserve(Map::serializedSize(first, last));
			Map::writeNodes(data, first, last);

			compressData(data, out, compression_acceleration_level, compression_level);
		} else {
			Map::writeNodes(out, first, last);
		}

		auto cur_index = out.writeIndex();
		size = cur_index - (size_index + sizeof(size));
		out.setWriteIndex(size_index);
		out.write(&size, sizeof(size));
		out.setWriteIndex(cur_index);
	}
};
}  // namespace ufo::map

#endif  // UFO_MAP_H