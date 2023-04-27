#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// A tool I used to characterize the seed space of the sfc8 random number
// generator. Some optimization techniques are used to improve the runtime.
// Probably the bit vector wasn't the way to go but I wasn't sure how many
// distinct cycles there were.

static const size_t POSSIBLE_STATES = (size_t)UINT32_MAX + 1;
static const size_t BIT_ARRAY_LENGTH = ((size_t)POSSIBLE_STATES+1)/64;
static const size_t BIT_ARRAY_SIZE = sizeof(uint64_t) * BIT_ARRAY_LENGTH;

// Bit vectors. While not the best way to do this, it works reasonably
// well for an unknown number of cycles.

size_t WORD_INDEX(const uint32_t value) {
    return value >> 6;
}

uint8_t BIT_INDEX(const uint32_t value) {
    return value & 0x3F;
}

bool test_and_set_bit(uint64_t *array, const uint32_t position) {
    uint64_t bit_word = 1ULL << BIT_INDEX(position);
    size_t word_index = WORD_INDEX(position);
    uint64_t array_word = array[word_index];
    array[word_index] = array_word | bit_word;
    return (array_word & bit_word) != 0;
}

bool test_bit(const uint64_t *array, const uint32_t position) {
    uint64_t bit_word = 1ULL << BIT_INDEX(position);
    size_t word_index = WORD_INDEX(position);
    uint64_t array_word = array[word_index];
    return (array_word & bit_word) != 0;
}

#define ARRAYS_TO_STORE 7
uint64_t* cycle_bit_array;
uint64_t* tested_cycle_bit_arrays[ARRAYS_TO_STORE];
static size_t saved_cycle_lengths[ARRAYS_TO_STORE] = {0};
static unsigned saved_array_count = 0;

typedef struct sfc8_s {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;

} sfc8_t;

void sfc8_advance(sfc8_t *state) {
    uint8_t temp = state->a + state->b + state->d++;
    state->a = state->b ^ (state->b >> 2);
    state->b = state->c + (state->c << 1);
    state->c = temp + ((state->c << 3)|(state->c >> 5));
}

uint32_t encode_state(const sfc8_t *state) {
    uint32_t result = state->a;
    result |= state->b << 8;
    result |= state->c << 16;
    result |= state->d << 24;
    return result;
}

bool check_existing_arrays(const sfc8_t *state, unsigned *ret_array_idx) {
    uint32_t encoded_state = encode_state(state);
    for (unsigned array_idx = 0; array_idx < saved_array_count; array_idx++) {
        *ret_array_idx = array_idx;
        if (test_bit(tested_cycle_bit_arrays[array_idx], encoded_state))
            return true;
    }
    return false;
}

// Update tested cycles with new cycles.
// This would be better if it used a heap, but there's only 4 entries so this isn't that bad.
bool update_tested_cycles(const size_t cycle_length) {
    if (saved_array_count == 0)
    {
        saved_array_count = 1;
        tested_cycle_bit_arrays[0] = cycle_bit_array;
        saved_cycle_lengths[0] = cycle_length;
        return true;
    }

    bool did_updates = false;

    uint64_t* array_to_consider = cycle_bit_array;
    uint64_t current_cycle_length = cycle_length;

    for (unsigned position = 0; position < ARRAYS_TO_STORE; position++)
    {
        uint64_t compared_cycle_length = saved_cycle_lengths[position];
        if(current_cycle_length > compared_cycle_length) {
            did_updates = true;
            uint64_t* prev_bit_array = tested_cycle_bit_arrays[position];
            tested_cycle_bit_arrays[position] = array_to_consider;
            saved_cycle_lengths[position] = current_cycle_length;

            if (prev_bit_array != NULL) {
                // This was the last slot. Just delete the old array.
                if (position == ARRAYS_TO_STORE - 1)
                {
                    free(prev_bit_array);
                }
                //Otherwise, prepare to search some more.
                else {
                    array_to_consider = prev_bit_array;
                    current_cycle_length = compared_cycle_length;
                }

            }
            else {
                saved_array_count++;
                break;
            }
        }
    }
    // If updates were done, the caller will need to allocate a new array.
    return did_updates;
}

size_t test_seed_for_cycle(const uint32_t seed, bool* was_on_known_cycle) {
    sfc8_t state;
    size_t length_counter;
    state.a = (uint8_t)seed;
    state.b = (uint8_t)(seed >> 8);
    state.c = (uint8_t)(seed >> 16);
    state.d = 1;

    if(saved_array_count > 0) {
        uint32_t return_index = 0;
        if(check_existing_arrays(&state, &return_index)) {
            *was_on_known_cycle = true;
            return saved_cycle_lengths[return_index];
        }
    }
    *was_on_known_cycle = false;
    memset(cycle_bit_array, 0, BIT_ARRAY_SIZE);

    bool collision;
    for (length_counter = 0; length_counter < POSSIBLE_STATES; length_counter++) {
        collision = test_and_set_bit(cycle_bit_array, encode_state(&state));
        if (collision) break;
        sfc8_advance(&state);
    }
    if (update_tested_cycles(length_counter))
    {
        // If true, this is a new long
        cycle_bit_array = malloc(BIT_ARRAY_SIZE);
        if (cycle_bit_array == NULL)
        {
            printf("Couldn't allocate more memory for state array...");
            exit(1);
        }
    }

    return length_counter;
}

int main(){
    for( unsigned i = 0; i < ARRAYS_TO_STORE; i++ ) {
        tested_cycle_bit_arrays[i] = NULL;
     }

    cycle_bit_array = malloc(BIT_ARRAY_SIZE);
    if (cycle_bit_array == NULL)
    {
        printf("Couldn't allocate memory for state array...");
        return 1;
    }
    printf("seed,length\n");
    for(uint32_t seed = 0; seed < (1U << 24); seed++)
    {
        bool was_on_known_cycle;
        size_t cycle_length = test_seed_for_cycle(seed, &was_on_known_cycle);
        if(!was_on_known_cycle)
            printf("0x%06X,%llu\n", seed, (unsigned long long)cycle_length);
    }
    free(cycle_bit_array);
    for (unsigned i= 0; i < saved_array_count; i++){
        free(tested_cycle_bit_arrays[i]);
    }

    return 0;
}
