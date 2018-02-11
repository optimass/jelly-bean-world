#ifndef NEL_GIBBS_FIELD_H_
#define NEL_GIBBS_FIELD_H_

#include "position.h"

namespace nel {

template<typename Map, typename IntensityFunction, typename InteractionFunction>
inline void sample_cell(
		Map& m,
		const position& patch_position,
		const position& position_within_patch)
{
	typedef typename Map::patch_type patch_type;
	static constexpr unsigned int n = Map::patch_size;
	static constexpr unsigned int item_type_count = Map::item_type_count;

	patch_type* neighborhood[4];
	position neighbor_positions[4];
	position world_position = patch_position * n + position_within_patch;

	unsigned int patch_index;
	unsigned int neighbor_count = m.get_neighborhood(world_position, neighborhood, neighbor_positions, patch_index);

	double log_probabilities[item_type_count + 1];
	unsigned int old_item_type = item_type_count;
	for (unsigned int i = 0; i < item_type_count; i++) {
		/* compute the energy contribution of this cell when the item type is `i` */
		log_probabilities[i] = IntensityFunction(world_position, i);
		for (unsigned int j = 0; j < neighbor_count; j++) {
			for (unsigned int k = 0; k < item_type_count; k++) {
				for (position item_position : neighborhood[j]->item_positions[k]) {
					if (item_position == world_position) {
						old_item_type = k; continue; /* ignore the current position */
					}

					log_probabilities[i] += InteractionFunction(world_position, item_position, i, k);
				}
			}
		}
	}

	log_probabilities[item_type_count] = 0.0;
	normalize_exp(log_probabilities, item_type_count);
	unsigned int sampled_item_type = sample_categorical(log_probabilities, item_type_count);

	patch_type& current_patch = *neighborhood[patch_index];
	if (old_item_type == sampled_item_type) {
		/* the Gibbs step didn't change anything */
		return;
	} if (old_item_type < item_type_count) {
		/* find and remove the old item position */
		for (unsigned int i = 0; i < current_patch.item_positions[old_item_type].length; i++) {
			if (current_patch.item_positions[old_item_type][i] == world_position) {
				current_patch.item_positions[old_item_type].remove(i);
				break;
			}
		}
	} if (sampled_item_type < item_type_count) {
		current_patch.item_positions[sampled_item_type].add(world_position);
	}
}

template<typename Map, typename IntensityFunction, typename InteractionFunction>
class gibbs_field
{
	Map& map;
	position* patch_positions;
	unsigned int patch_count;

	static constexpr unsigned int n = Map::patch_size;
	typedef typename Map::patch_type patch_type;

public:
	/* NOTE: `patches` and `patch_positions` are used directly, and not copied */
	gibbs_field(Map& map, position* patch_positions, unsigned int patch_count) :
			map(map), patch_positions(patch_positions), patch_count(patch_count) { }

	~gibbs_field() { }

	void sample() {
		for (unsigned int i = 0; i < patch_count; i++)
			sample_patch(patch_positions[i]);
	}

private:
	inline void sample_patch(const position& patch_position) {
		for (unsigned int x = 0; x < n; x++)
			for (unsigned int y = 0; y < n; y++)
				sample_cell<Map, IntensityFunction, InteractionFunction>(map, patch_position, {x, y});
	}
};

} /* namespace nel */

#endif /* NEL_GIBBS_FIELD_H_ */
