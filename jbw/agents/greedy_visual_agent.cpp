#include <jbw/simulator.h>
#include <jbw/mpi.h>
#include <set>

using namespace core;
using namespace jbw;

inline void set_interaction_args(
		item_properties* item_types, unsigned int first_item_type,
		unsigned int second_item_type, interaction_function interaction,
		std::initializer_list<float> args)
{
	item_types[first_item_type].interaction_fns[second_item_type].fn = interaction;
	item_types[first_item_type].interaction_fns[second_item_type].arg_count = (unsigned int) args.size();
	item_types[first_item_type].interaction_fns[second_item_type].args = (float*) malloc(max((size_t) 1, sizeof(float) * args.size()));

	unsigned int counter = 0;
	for (auto i = args.begin(); i != args.end(); i++)
		item_types[first_item_type].interaction_fns[second_item_type].args[counter++] = *i;
}

inline void on_step(const simulator<empty_data>* sim,
		const hash_map<uint64_t, agent_state*>& agents, uint64_t time)
{ }

struct shortest_path_state
{
	unsigned int cost;
	int x, y;
	shortest_path_state* prev;
	direction dir;

	unsigned int reference_count;

	static inline void free(shortest_path_state& state) {
		state.reference_count--;
		if (state.reference_count == 0 && state.prev != nullptr) {
			core::free(*state.prev);
			if (state.prev->reference_count == 0)
				core::free(state.prev);
		}
	}

	struct less_than {
		inline bool operator () (const shortest_path_state* left, const shortest_path_state* right) {
			return left->cost < right->cost;
		}
	};
};

inline bool item_exists(
		const float* vision, int vision_range,
		unsigned int color_dimension,
		const float* item_color, int x, int y)
{
	unsigned int offset = ((x + vision_range) * (2*vision_range + 1) + (y + vision_range)) * color_dimension;
	for (unsigned int i = 0; i < color_dimension; i++)
		if (vision[offset + i] != item_color[i]) return false;

	return true;
}

inline void move_forward(int x, int y, direction dir, int& new_x, int& new_y) {
	new_x = x;
	new_y = y;
	if (dir == direction::UP) new_y++;
	else if (dir == direction::DOWN) new_y--;
	else if (dir == direction::LEFT) new_x--;
	else if (dir == direction::RIGHT) new_x++;
}

inline direction turn_left(direction dir) {
	if (dir == direction::UP) return direction::LEFT;
	else if (dir == direction::DOWN) return direction::RIGHT;
	else if (dir == direction::LEFT) return direction::DOWN;
	else if (dir == direction::RIGHT) return direction::UP;
	fprintf(stderr, "turn_left: Unrecognized direction.\n");
	exit(EXIT_FAILURE);
}

inline direction turn_right(direction dir) {
	if (dir == direction::UP) return direction::RIGHT;
	else if (dir == direction::DOWN) return direction::LEFT;
	else if (dir == direction::LEFT) return direction::UP;
	else if (dir == direction::RIGHT) return direction::DOWN;
	fprintf(stderr, "turn_right: Unrecognized direction.\n");
	exit(EXIT_FAILURE);
}

shortest_path_state* shortest_path(
		const float* vision, int vision_range,
		const float* jellybean_color,
		const float* wall_color,
		unsigned int color_dimension)
{
	unsigned int* smallest_costs = (unsigned int*) malloc(sizeof(unsigned int) * (2*vision_range + 1) * (2*vision_range + 1) * 4);
	for (unsigned int i = 0; i < (2*vision_range + 1) * (2*vision_range + 1) * 4; i++)
		smallest_costs[i] = UINT_MAX;

	std::multiset<shortest_path_state*, shortest_path_state::less_than> queue;
	shortest_path_state* initial_state = (shortest_path_state*) malloc(sizeof(shortest_path_state));
	initial_state->cost = 0;
	initial_state->x = 0;
	initial_state->y = 0;
	initial_state->reference_count = 1;
	initial_state->prev = nullptr;
	initial_state->dir = direction::UP;
	smallest_costs[((vision_range + 0)*(2*vision_range + 1) + (vision_range + 0))*4 + (int) direction::UP] = 0;
	queue.insert(initial_state);

	shortest_path_state* shortest_path = nullptr;
	while (!queue.empty()) {
		auto first = queue.cbegin();
		shortest_path_state* state = *first;
		queue.erase(first);

		/* check if we found a jellybean */
		if (!(state->x == 0 && state->y == 0) && item_exists(vision, vision_range, color_dimension, jellybean_color, state->x, state->y)) {
			/* we found a jellybean, we can stop the search */
			shortest_path = state;
			break;
		}

		/* consider moving forward */
		int new_x, new_y;
		direction new_dir = state->dir;
		move_forward(state->x, state->y, state->dir, new_x, new_y);
		if (new_x >= -vision_range && new_x <= vision_range && new_y >= -vision_range && new_y <= vision_range) {
			/* check if there is a wall in the new position */
			if (!item_exists(vision, vision_range, color_dimension, wall_color, new_x, new_y)) {
				/* there is no wall, so continue considering this movement */
				unsigned int new_cost = state->cost + 1;
				if (new_cost < smallest_costs[((vision_range + new_x)*(2*vision_range + 1) + (vision_range + new_y))*4 + (int) new_dir]) {
					smallest_costs[((vision_range + new_x)*(2*vision_range + 1) + (vision_range + new_y))*4 + (int) new_dir] = new_cost;

					shortest_path_state* new_state = (shortest_path_state*) malloc(sizeof(shortest_path_state));
					new_state->cost = new_cost;
					new_state->x = new_x;
					new_state->y = new_y;
					new_state->dir = new_dir;
					new_state->reference_count = 1;
					new_state->prev = state;
					state->reference_count++;
					queue.insert(new_state);
				}
			}
		}

		/* consider turning left */
		unsigned int new_cost = state->cost + 1;
		new_x = state->x;
		new_y = state->y;
		new_dir = turn_left(state->dir);
		if (new_cost < smallest_costs[((vision_range + new_x)*(2*vision_range + 1) + (vision_range + new_y))*4 + (int) new_dir]) {
			smallest_costs[((vision_range + new_x)*(2*vision_range + 1) + (vision_range + new_y))*4 + (int) new_dir] = new_cost;

			shortest_path_state* new_state = (shortest_path_state*) malloc(sizeof(shortest_path_state));
			new_state->cost = new_cost;
			new_state->x = new_x;
			new_state->y = new_y;
			new_state->dir = new_dir;
			new_state->reference_count = 1;
			new_state->prev = state;
			state->reference_count++;
			queue.insert(new_state);
		}

		/* consider turning right */
		new_cost = state->cost + 1;
		new_x = state->x;
		new_y = state->y;
		new_dir = turn_right(state->dir);
		if (new_cost < smallest_costs[((vision_range + new_x)*(2*vision_range + 1) + (vision_range + new_y))*4 + (int) new_dir]) {
			smallest_costs[((vision_range + new_x)*(2*vision_range + 1) + (vision_range + new_y))*4 + (int) new_dir] = new_cost;

			shortest_path_state* new_state = (shortest_path_state*) malloc(sizeof(shortest_path_state));
			new_state->cost = new_cost;
			new_state->x = new_x;
			new_state->y = new_y;
			new_state->dir = new_dir;
			new_state->reference_count = 1;
			new_state->prev = state;
			state->reference_count++;
			queue.insert(new_state);
		}

		free(*state);
		if (state->reference_count == 0)
			free(state);
	}

	for (auto state : queue) {
		free(*state);
		if (state->reference_count == 0)
			free(state);
	}

	free(smallest_costs);
	return shortest_path;
}

int main(int argc, const char** argv)
{
	set_seed(0);

	simulator_config config;
	config.max_steps_per_movement = 1;
	config.scent_dimension = 3;
	config.color_dimension = 3;
	config.vision_range = 5;
	config.allowed_movement_directions[0] = action_policy::ALLOWED;
	config.allowed_movement_directions[1] = action_policy::DISALLOWED;
	config.allowed_movement_directions[2] = action_policy::DISALLOWED;
	config.allowed_movement_directions[3] = action_policy::DISALLOWED;
	config.allowed_rotations[0] = action_policy::DISALLOWED;
	config.allowed_rotations[1] = action_policy::DISALLOWED;
	config.allowed_rotations[2] = action_policy::ALLOWED;
	config.allowed_rotations[3] = action_policy::ALLOWED;
	config.no_op_allowed = false;
	config.patch_size = 32;
	config.mcmc_iterations = 4000;
	config.agent_color = (float*) calloc(config.color_dimension, sizeof(float));
	config.agent_color[2] = 1.0f;
	config.collision_policy = movement_conflict_policy::FIRST_COME_FIRST_SERVED;
	config.decay_param = 0.4f;
	config.diffusion_param = 0.14f;
	config.deleted_item_lifetime = 2000;

	/* configure item types */
	unsigned int item_type_count = 4;
	config.item_types.ensure_capacity(item_type_count);
	config.item_types[0].name = "banana";
	config.item_types[0].scent = (float*) calloc(config.scent_dimension, sizeof(float));
	config.item_types[0].color = (float*) calloc(config.color_dimension, sizeof(float));
	config.item_types[0].required_item_counts = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[0].required_item_costs = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[0].scent[1] = 1.0f;
	config.item_types[0].color[1] = 1.0f;
	config.item_types[0].required_item_counts[0] = 1;
	config.item_types[0].blocks_movement = false;
	config.item_types[1].name = "onion";
	config.item_types[1].scent = (float*) calloc(config.scent_dimension, sizeof(float));
	config.item_types[1].color = (float*) calloc(config.color_dimension, sizeof(float));
	config.item_types[1].required_item_counts = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[1].required_item_costs = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[1].scent[0] = 1.0f;
	config.item_types[1].color[0] = 1.0f;
	config.item_types[1].required_item_counts[1] = 1;
	config.item_types[1].blocks_movement = false;
	config.item_types[2].name = "jellybean";
	config.item_types[2].scent = (float*) calloc(config.scent_dimension, sizeof(float));
	config.item_types[2].color = (float*) calloc(config.color_dimension, sizeof(float));
	config.item_types[2].required_item_counts = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[2].required_item_costs = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[2].scent[2] = 1.0f;
	config.item_types[2].color[2] = 1.0f;
	config.item_types[2].blocks_movement = false;
	config.item_types[3].name = "wall";
	config.item_types[3].scent = (float*) calloc(config.scent_dimension, sizeof(float));
	config.item_types[3].color = (float*) calloc(config.color_dimension, sizeof(float));
	config.item_types[3].required_item_counts = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[3].required_item_costs = (unsigned int*) calloc(item_type_count, sizeof(unsigned int));
	config.item_types[3].color[0] = 0.5f;
	config.item_types[3].color[1] = 0.5f;
	config.item_types[3].color[2] = 0.5f;
	config.item_types[3].required_item_counts[3] = 1;
	config.item_types[3].blocks_movement = true;
	config.item_types.length = item_type_count;

	config.item_types[0].intensity_fn.fn = constant_intensity_fn;
	config.item_types[0].intensity_fn.arg_count = 1;
	config.item_types[0].intensity_fn.args = (float*) malloc(sizeof(float) * 1);
	config.item_types[0].intensity_fn.args[0] = -5.3f;
	config.item_types[0].interaction_fns = (energy_function<interaction_function>*)
			malloc(sizeof(energy_function<interaction_function>) * config.item_types.length);
	config.item_types[1].intensity_fn.fn = constant_intensity_fn;
	config.item_types[1].intensity_fn.arg_count = 1;
	config.item_types[1].intensity_fn.args = (float*) malloc(sizeof(float) * 1);
	config.item_types[1].intensity_fn.args[0] = -5.0f;
	config.item_types[1].interaction_fns = (energy_function<interaction_function>*)
			malloc(sizeof(energy_function<interaction_function>) * config.item_types.length);
	config.item_types[2].intensity_fn.fn = constant_intensity_fn;
	config.item_types[2].intensity_fn.arg_count = 1;
	config.item_types[2].intensity_fn.args = (float*) malloc(sizeof(float) * 1);
	config.item_types[2].intensity_fn.args[0] = -5.3f;
	config.item_types[2].interaction_fns = (energy_function<interaction_function>*)
			malloc(sizeof(energy_function<interaction_function>) * config.item_types.length);
	config.item_types[3].intensity_fn.fn = constant_intensity_fn;
	config.item_types[3].intensity_fn.arg_count = 1;
	config.item_types[3].intensity_fn.args = (float*) malloc(sizeof(float) * 1);
	config.item_types[3].intensity_fn.args[0] = 0.0f;
	config.item_types[3].interaction_fns = (energy_function<interaction_function>*)
			malloc(sizeof(energy_function<interaction_function>) * config.item_types.length);

	set_interaction_args(config.item_types.data, 0, 0, piecewise_box_interaction_fn, {10.0f, 200.0f, 0.0f, -6.0f});
	set_interaction_args(config.item_types.data, 0, 1, piecewise_box_interaction_fn, {200.0f, 0.0f, -6.0f, -6.0f});
	set_interaction_args(config.item_types.data, 0, 2, piecewise_box_interaction_fn, {10.0f, 200.0f, 2.0f, -100.0f});
	set_interaction_args(config.item_types.data, 0, 3, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 1, 0, piecewise_box_interaction_fn, {200.0f, 0.0f, -6.0f, -6.0f});
	set_interaction_args(config.item_types.data, 1, 1, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 1, 2, piecewise_box_interaction_fn, {200.0f, 0.0f, -100.0f, -100.0f});
	set_interaction_args(config.item_types.data, 1, 3, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 2, 0, piecewise_box_interaction_fn, {10.0f, 200.0f, 2.0f, -100.0f});
	set_interaction_args(config.item_types.data, 2, 1, piecewise_box_interaction_fn, {200.0f, 0.0f, -100.0f, -100.0f});
	set_interaction_args(config.item_types.data, 2, 2, piecewise_box_interaction_fn, {10.0f, 200.0f, 0.0f, -6.0f});
	set_interaction_args(config.item_types.data, 2, 3, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 3, 0, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 3, 1, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 3, 2, zero_interaction_fn, {});
	set_interaction_args(config.item_types.data, 3, 3, cross_interaction_fn, {10.0f, 15.0f, 20.0f, -200.0f, -20.0f, 1.0f});

	unsigned int jellybean_index = config.item_types.length;
	unsigned int wall_index = config.item_types.length;
	for (unsigned int i = 0; i < config.item_types.length; i++) {
		if (config.item_types[i].name == "jellybean") {
			jellybean_index = i;
		} else if (config.item_types[i].name == "wall") {
			wall_index = i;
		}
	}

	if (jellybean_index == config.item_types.length) {
		fprintf(stderr, "ERROR: There is no item named 'jellybean'.\n");
		return false;
	} if (wall_index == config.item_types.length) {
		fprintf(stderr, "WARNING: There is no item named 'wall'.\n");
	}

	const float* jellybean_color = config.item_types[jellybean_index].color;
	float* wall_color = (float*) alloca(sizeof(float) * config.color_dimension);
	if (wall_index == config.item_types.length) {
		for (unsigned int i = 0; i < config.color_dimension; i++)
			wall_color[i] = -1.0f;
	} else {
		for (unsigned int i = 0; i < config.color_dimension; i++)
			wall_color[i] = config.item_types[wall_index].color[i];
	}

	simulator<empty_data>& sim = *((simulator<empty_data>*) alloca(sizeof(simulator<empty_data>)));
	if (init(sim, config, empty_data(), get_seed()) != status::OK) {
		fprintf(stderr, "ERROR: Unable to initialize simulator.\n");
		return false;
	}

	async_server server;
	bool server_started = true;
	if (!init_server(server, sim, 54353, 256, 8, permissions::grant_all())) {
		fprintf(stderr, "WARNING: Unable to start server.\n");
		server_started = false;
	}

	uint64_t agent_id; agent_state* agent;
	sim.add_agent(agent_id, agent);

	shortest_path_state* best_path = nullptr;
	unsigned int current_path_position = 0; /* zero-based index of current position in `best_path` */
	unsigned int current_path_length = 0; /* number of non-null states in `best_path` */
	for (unsigned int t = 0; true; t++)
	{
		shortest_path_state* new_path = shortest_path(
				agent->current_vision, config.vision_range,
				jellybean_color, wall_color, config.color_dimension);

		if (best_path == nullptr || (new_path != nullptr && new_path->cost < best_path->cost - current_path_position))
		{
			if (best_path != nullptr) {
				free(*best_path);
				if (best_path->reference_count == 0)
					free(best_path);
			}
			best_path = new_path;
			current_path_position = 0;
			current_path_length = 0;

			shortest_path_state* curr = new_path;
			while (curr != nullptr) {
				current_path_length++;
				curr = curr->prev;
			}
			if (new_path != nullptr) new_path->reference_count++;
		}

		if (new_path != nullptr) {
			free(*new_path);
			if (new_path->reference_count == 0)
				free(new_path);
		}

		if (best_path == nullptr) {
			if (!item_exists(agent->current_vision, config.vision_range, config.color_dimension, wall_color, 0, 1)) {
				sim.move(agent_id, direction::UP, 1);
			} else if (rand() % 2 == 0) {
				sim.turn(agent_id, direction::LEFT);
			} else {
				sim.turn(agent_id, direction::RIGHT);
			}
		} else {
			shortest_path_state* curr = best_path;
			shortest_path_state* next = nullptr;
			for (unsigned int i = current_path_length - 1; i > current_path_position; i--) {
				next = curr;
				curr = curr->prev;
			}

			int new_x, new_y;
			move_forward(curr->x, curr->y, curr->dir, new_x, new_y);
			if (new_x == next->x && new_y == next->y) {
				sim.move(agent_id, direction::UP, 1);
			} else if (next->dir == turn_left(curr->dir)) {
				sim.turn(agent_id, direction::LEFT);
			} else if (next->dir == turn_right(curr->dir)) {
				sim.turn(agent_id, direction::RIGHT);
			} else {
				fprintf(stderr, "ERROR: `shortest_path` returned an invalid path.\n");
			}
			current_path_position++;

			if (current_path_position + 1 >= current_path_length) {
				free(*best_path);
				if (best_path->reference_count == 0)
					free(best_path);
				best_path = nullptr;
			}
		}

		if (t % 1000 == 0) {
			fprintf(stderr, "[iteration %u] Current agent state: ", t);
			print(agent->current_position, stderr); fprintf(stderr, ", %u, %lf\n",
					agent->collected_items[jellybean_index],
					(double) agent->collected_items[jellybean_index] / (t + 1));
		}
	}

	if (best_path != nullptr) {
		free(*best_path);
		if (best_path->reference_count == 0)
			free(best_path);
	}

	if (server_started)
		stop_server(server);
	free(sim);
}
