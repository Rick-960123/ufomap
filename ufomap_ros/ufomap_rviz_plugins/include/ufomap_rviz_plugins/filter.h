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

#ifndef UFOMAP_RVIZ_PLUGINS_FILTER_H
#define UFOMAP_RVIZ_PLUGINS_FILTER_H

// UFO
#include <ufo/geometry/aabb.h>
#include <ufo/map/semantic/semantic.h>
#include <ufo/map/types.h>

// STD
#include <cstdint>

namespace ufomap_ros::rviz_plugins
{
struct Filter {
	// Occupancy
	bool filter_occupancy;
	uint8_t min_occupancy;
	uint8_t max_occupancy;

	// Time step
	bool filter_time_step;
	ufo::map::time_step_t min_time_step;
	ufo::map::time_step_t max_time_step;

	// Semantics
	bool filter_semantics;
	// TODO: Add labels
	ufo::map::semantic_value_t min_semantic_value;
	ufo::map::semantic_value_t max_semantic_value;

	// Bounding volume
	bool filter_bounding_volume;
	ufo::geometry::AABB bounding_volume;

	bool operator==(Filter const& rhs) const
	{
		bool occ = rhs.filter_occupancy == filter_occupancy &&
		           (!filter_occupancy || (rhs.min_occupancy == min_occupancy &&
		                                  rhs.max_occupancy == max_occupancy));

		bool ts = rhs.filter_time_step == filter_time_step &&
		          (!filter_time_step || (rhs.min_time_step == min_time_step &&
		                                 rhs.max_time_step == max_time_step));

		bool sem = rhs.filter_semantics == filter_semantics &&
		           (!filter_semantics || (rhs.min_semantic_value == min_semantic_value &&
		                                  rhs.max_semantic_value == max_semantic_value));

		bool bv = rhs.filter_bounding_volume == filter_bounding_volume &&
		          (!filter_bounding_volume || (rhs.bounding_volume == bounding_volume));

		return occ && ts && sem && bv;
	}

	bool operator!=(Filter const& rhs) const { return !(*this == rhs); }
};
}  // namespace ufomap_ros::rviz_plugins

#endif  // UFOMAP_RVIZ_PLUGINS_FILTER_H