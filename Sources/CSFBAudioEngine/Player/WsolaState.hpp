//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <utility>
#include <limits>
#include <cassert>
#include <sys/types.h>

namespace sfb {

inline std::vector<float> get_symmetric_hanning_window(size_t window_length) {
    std::vector<float> window(window_length, 0.0f);
    float scale = 2.0f * M_PI / window_length;
    for (size_t n = 0; n < window_length; ++n) {
        window[n] = 0.5f * (1.0f - std::cos(n * scale));
    }
    return window;
}

inline void dot_product_multi(
    const std::vector<std::vector<float>>& a,
    size_t frame_offset_a,
    const std::vector<std::vector<float>>& b,
    size_t frame_offset_b,
    size_t channels,
    size_t num_frames,
    float* dot_product
) {
    for (size_t k = 0; k < channels; ++k) {
        const float* ch_a = &a[k][frame_offset_a];
        const float* ch_b = &b[k][frame_offset_b];
        float sum = 0.0f;
        for (size_t n = 0; n < num_frames; ++n) {
            sum += ch_a[n] * ch_b[n];
        }
        dot_product[k] = sum;
    }
}

inline float similarity_measure(
    const float* dot_prod,
    const float* energy_target,
    const float* energy_candidate,
    size_t channels
) {
    float epsilon = 1e-12f;
    float sum = 0.0f;
    for (size_t n = 0; n < channels; ++n) {
        sum += dot_prod[n] * energy_target[n]
            / std::sqrt(energy_target[n] * energy_candidate[n] + epsilon);
    }
    return sum;
}

inline void quadratic_interpolation(
    const float y_values[3],
    float& extremum,
    float& extremum_value
) {
    float a = 0.5f * (y_values[2] + y_values[0]) - y_values[1];
    float b = 0.5f * (y_values[2] - y_values[0]);
    float c = y_values[1];

    if (a == 0.0f) {
        extremum = 0.0f;
        extremum_value = y_values[1];
    } else {
        extremum = -b / (2.0f * a);
        extremum_value = a * extremum * extremum + b * extremum + c;
    }
}

inline void multi_channel_moving_block_energies(
    const std::vector<std::vector<float>>& input,
    size_t channels,
    size_t frames_per_block,
    float* energy
) {
    size_t input_frames = input[0].size();
    size_t num_blocks = input_frames - (frames_per_block - 1);

    for (size_t k = 0; k < channels; ++k) {
        const auto& input_channel = input[k];

        // First block of channel k
        float sum = 0.0f;
        for (size_t m = 0; m < frames_per_block; ++m) {
            float val = input_channel[m];
            sum += val * val;
        }
        energy[k] = sum;

        for (size_t n = 1; n < num_blocks; ++n) {
            float slide_out = input_channel[n - 1];
            float slide_in = input_channel[n + frames_per_block - 1];
            energy[k + n * channels] = energy[k + (n - 1) * channels]
                - slide_out * slide_out
                + slide_in * slide_in;
        }
    }
}

inline void peek_audio_with_zero_prepend(
    const std::vector<std::vector<float>>& input_buffer,
    size_t channels,
    ssize_t read_offset_frames,
    std::vector<std::vector<float>>& dest,
    size_t dest_frames
) {
    size_t write_offset = 0;
    size_t num_frames_to_read = dest_frames;
    ssize_t actual_read_offset = read_offset_frames;

    if (read_offset_frames < 0) {
        size_t num_zero_frames_appended = (size_t)(-read_offset_frames);
        num_zero_frames_appended = std::min(num_zero_frames_appended, num_frames_to_read);
        actual_read_offset = 0;
        num_frames_to_read -= num_zero_frames_appended;
        write_offset = num_zero_frames_appended;

        for (size_t ch = 0; ch < channels; ++ch) {
            std::fill(dest[ch].begin(), dest[ch].begin() + num_zero_frames_appended, 0.0f);
        }
    }

    if (num_frames_to_read > 0) {
        for (size_t i = 0; i < channels; ++i) {
            std::copy(
                input_buffer[i].begin() + actual_read_offset,
                input_buffer[i].begin() + actual_read_offset + num_frames_to_read,
                dest[i].begin() + write_offset
            );
        }
    }
}

inline size_t decimated_search(
    size_t decimation,
    std::pair<ssize_t, ssize_t> exclude_interval,
    const std::vector<std::vector<float>>& target_block,
    size_t target_block_frames,
    const std::vector<std::vector<float>>& search_segment,
    size_t search_segment_frames,
    size_t channels,
    const float* energy_target_block,
    const float* energy_candidate_blocks
) {
    size_t num_candidate_blocks = search_segment_frames - (target_block_frames - 1);
    std::vector<float> dot_prod(channels, 0.0f);
    float similarity[3] = {0.0f, 0.0f, 0.0f};

    size_t n = 0;
    dot_product_multi(
        target_block, 0,
        search_segment, n,
        channels,
        target_block_frames,
        dot_prod.data()
    );
    similarity[0] = similarity_measure(
        dot_prod.data(),
        energy_target_block,
        &energy_candidate_blocks[0],
        channels
    );

    float best_similarity = similarity[0];
    size_t optimal_index = 0;

    n += decimation;
    if (n >= num_candidate_blocks) {
        return 0;
    }

    dot_product_multi(
        target_block, 0,
        search_segment, n,
        channels,
        target_block_frames,
        dot_prod.data()
    );
    similarity[1] = similarity_measure(
        dot_prod.data(),
        energy_target_block,
        &energy_candidate_blocks[n * channels],
        channels
    );

    n += decimation;
    if (n >= num_candidate_blocks) {
        return (similarity[1] > similarity[0]) ? decimation : 0;
    }

    while (n < num_candidate_blocks) {
        dot_product_multi(
            target_block, 0,
            search_segment, n,
            channels,
            target_block_frames,
            dot_prod.data()
        );
        similarity[2] = similarity_measure(
            dot_prod.data(),
            energy_target_block,
            &energy_candidate_blocks[n * channels],
            channels
        );

        if ((similarity[1] > similarity[0] && similarity[1] >= similarity[2]) ||
            (similarity[1] >= similarity[0] && similarity[1] > similarity[2]))
        {
            float normalized_candidate_index = 0.0f;
            float candidate_similarity = 0.0f;
            quadratic_interpolation(similarity, normalized_candidate_index, candidate_similarity);

            ssize_t candidate_index = (ssize_t)(n - decimation)
                + (ssize_t)std::floor(normalized_candidate_index * (float)decimation + 0.5f);
            
            bool in_exclude = candidate_index >= exclude_interval.first && candidate_index <= exclude_interval.second;
            if (candidate_similarity > best_similarity && !in_exclude) {
                optimal_index = (size_t)std::max<ssize_t>(candidate_index, 0);
                best_similarity = candidate_similarity;
            }
        } else if (n + decimation >= num_candidate_blocks) {
            bool in_exclude = (ssize_t)n >= exclude_interval.first && (ssize_t)n <= exclude_interval.second;
            if (similarity[2] > best_similarity && !in_exclude) {
                optimal_index = n;
                best_similarity = similarity[2];
            }
        }

        similarity[0] = similarity[1];
        similarity[1] = similarity[2];
        n += decimation;
    }

    return optimal_index;
}

inline size_t full_search(
    size_t low_limit,
    size_t high_limit,
    std::pair<ssize_t, ssize_t> exclude_interval,
    const std::vector<std::vector<float>>& target_block,
    size_t target_block_frames,
    const std::vector<std::vector<float>>& search_block,
    size_t search_block_frames,
    size_t channels,
    const float* energy_target_block,
    const float* energy_candidate_blocks
) {
    std::vector<float> dot_prod(channels, 0.0f);
    float best_similarity = -std::numeric_limits<float>::max();
    size_t optimal_index = 0;

    for (size_t n = low_limit; n <= high_limit; ++n) {
        ssize_t n_isize = (ssize_t)n;
        if (n_isize >= exclude_interval.first && n_isize <= exclude_interval.second) {
            continue;
        }

        dot_product_multi(
            target_block, 0,
            search_block, n,
            channels,
            target_block_frames,
            dot_prod.data()
        );

        float similarity = similarity_measure(
            dot_prod.data(),
            energy_target_block,
            &energy_candidate_blocks[n * channels],
            channels
        );

        if (similarity > best_similarity) {
            best_similarity = similarity;
            optimal_index = n;
        }
    }

    return optimal_index;
}

inline size_t compute_optimal_index(
    const std::vector<std::vector<float>>& search_block,
    size_t search_block_frames,
    const std::vector<std::vector<float>>& target_block,
    size_t target_block_frames,
    float* energy_candidate_blocks,
    size_t channels,
    std::pair<ssize_t, ssize_t> exclude_interval
) {
    size_t num_candidate_blocks = search_block_frames - (target_block_frames - 1);
    size_t search_decimation = 5;
    std::vector<float> energy_target_block(channels, 0.0f);

    multi_channel_moving_block_energies(
        search_block,
        channels,
        target_block_frames,
        energy_candidate_blocks
    );

    dot_product_multi(
        target_block, 0,
        target_block, 0,
        channels,
        target_block_frames,
        energy_target_block.data()
    );

    size_t optimal_index = decimated_search(
        search_decimation,
        exclude_interval,
        target_block,
        target_block_frames,
        search_block,
        search_block_frames,
        channels,
        energy_target_block.data(),
        energy_candidate_blocks
    );

    size_t lim_low = (optimal_index >= search_decimation) ? (optimal_index - search_decimation) : 0;
    size_t lim_high = std::min<size_t>(optimal_index + search_decimation, num_candidate_blocks - 1);

    return full_search(
        lim_low,
        lim_high,
        exclude_interval,
        target_block,
        target_block_frames,
        search_block,
        search_block_frames,
        channels,
        energy_target_block.data(),
        energy_candidate_blocks
    );
}

class WsolaState {
public:
    float min_playback_rate;
    float max_playback_rate;
    float ola_window_size_ms;
    float wsola_search_interval_ms;

    size_t channels;
    uint32_t sample_rate;

    double muted_partial_frame;
    double output_time;
    size_t search_block_center_offset;
    ssize_t search_block_index;
    size_t num_candidate_blocks;
    ssize_t target_block_index;
    size_t ola_window_size;
    size_t ola_hop_size;
    size_t num_complete_frames;
    bool wsola_output_started;

    std::vector<float> ola_window;
    std::vector<float> transition_window;

    std::vector<std::vector<float>> wsola_output;
    size_t wsola_output_size;
    std::vector<std::vector<float>> optimal_block;
    std::vector<std::vector<float>> search_block;
    size_t search_block_size;
    std::vector<std::vector<float>> target_block;
    std::vector<std::vector<float>> input_buffer;

    size_t input_buffer_final_frames;
    size_t input_buffer_added_silence;
    std::vector<float> energy_candidate_blocks;
    size_t optimal_index;

    WsolaState(
        size_t channels,
        uint32_t sample_rate,
        float min_playback_rate = 0.25f,
        float max_playback_rate = 8.0f,
        float ola_window_size_ms = 12.0f,
        float wsola_search_interval_ms = 40.0f
    ) : min_playback_rate(min_playback_rate),
        max_playback_rate(max_playback_rate),
        ola_window_size_ms(ola_window_size_ms),
        wsola_search_interval_ms(wsola_search_interval_ms),
        channels(channels),
        sample_rate(sample_rate)
    {
        num_candidate_blocks = (size_t)(wsola_search_interval_ms * (float)sample_rate / 1000.0f);
        ola_window_size = (size_t)(ola_window_size_ms * (float)sample_rate / 1000.0f);
        ola_window_size += ola_window_size & 1;
        ola_hop_size = ola_window_size / 2;

        search_block_center_offset = num_candidate_blocks / 2 + (ola_window_size / 2 - 1);
        ola_window = get_symmetric_hanning_window(ola_window_size);
        transition_window = get_symmetric_hanning_window(2 * ola_window_size);

        wsola_output_size = ola_window_size + ola_hop_size;

        wsola_output.assign(channels, std::vector<float>(wsola_output_size, 0.0f));
        optimal_block.assign(channels, std::vector<float>(ola_window_size, 0.0f));
        search_block_size = num_candidate_blocks + (ola_window_size - 1);
        search_block.assign(channels, std::vector<float>(search_block_size, 0.0f));
        target_block.assign(channels, std::vector<float>(ola_window_size, 0.0f));
        
        size_t initial_size = 4 * std::max(ola_window_size, search_block_size);
        input_buffer.assign(channels, std::vector<float>());
        for (size_t i = 0; i < channels; ++i) {
            input_buffer[i].reserve(initial_size);
        }

        energy_candidate_blocks.assign(channels * num_candidate_blocks, 0.0f);

        reset();
    }

    void reset() {
        for (size_t ch = 0; ch < channels; ++ch) {
            input_buffer[ch].clear();
            std::fill(wsola_output[ch].begin(), wsola_output[ch].end(), 0.0f);
        }
        input_buffer_final_frames = 0;
        input_buffer_added_silence = 0;
        output_time = 0.0;
        search_block_index = 0;
        target_block_index = 0;
        num_complete_frames = 0;
        wsola_output_started = false;
        muted_partial_frame = 0.0;
    }

    size_t input_buffer_frames() const {
        return input_buffer[0].size();
    }

    void seek_buffer(size_t frames) {
        assert(input_buffer_frames() >= frames);
        if (input_buffer_final_frames > 0) {
            input_buffer_final_frames = (input_buffer_final_frames >= frames) ? (input_buffer_final_frames - frames) : 0;
        }
        for (size_t i = 0; i < channels; ++i) {
            input_buffer[i].erase(input_buffer[i].begin(), input_buffer[i].begin() + frames);
        }
    }

    void set_final() {
        if (input_buffer_final_frames == 0) {
            input_buffer_final_frames = input_buffer_frames();
        }
    }

    bool target_is_within_search_region() const {
        return target_block_index >= search_block_index
            && target_block_index + (ssize_t)ola_window_size
                <= search_block_index + (ssize_t)search_block_size;
    }

    void get_optimal_block() {
        ssize_t exclude_interval_length_frames = 160;
        if (target_is_within_search_region()) {
            optimal_index = (size_t)target_block_index;
            peek_audio_with_zero_prepend(
                input_buffer,
                channels,
                target_block_index,
                optimal_block,
                ola_window_size
            );
        } else {
            peek_audio_with_zero_prepend(
                input_buffer,
                channels,
                target_block_index,
                target_block,
                ola_window_size
            );
            peek_audio_with_zero_prepend(
                input_buffer,
                channels,
                search_block_index,
                search_block,
                search_block_size
            );

            ssize_t last_optimal = target_block_index
                - (ssize_t)ola_hop_size
                - search_block_index;
            std::pair<ssize_t, ssize_t> exclude_interval = {
                last_optimal - exclude_interval_length_frames / 2,
                last_optimal + exclude_interval_length_frames / 2
            };

            size_t opt_idx = compute_optimal_index(
                search_block,
                search_block_size,
                target_block,
                ola_window_size,
                energy_candidate_blocks.data(),
                channels,
                exclude_interval
            );

            optimal_index = (size_t)((ssize_t)opt_idx + search_block_index);
            peek_audio_with_zero_prepend(
                input_buffer,
                channels,
                (ssize_t)optimal_index,
                optimal_block,
                ola_window_size
            );

            for (size_t k = 0; k < channels; ++k) {
                auto& opt = optimal_block[k];
                const auto& tgt = target_block[k];
                for (size_t n = 0; n < ola_window_size; ++n) {
                    opt[n] = opt[n] * transition_window[n]
                        + tgt[n] * transition_window[ola_window_size + n];
                }
            }
        }

        target_block_index = (ssize_t)(optimal_index + ola_hop_size);
    }

    double get_updated_time(float playback_rate) const {
        return output_time + (double)ola_hop_size * (double)playback_rate;
    }

    ssize_t get_search_block_index(double out_time) const {
        return (ssize_t)std::floor(out_time - (double)search_block_center_offset + 0.5);
    }

    ssize_t frames_needed(float playback_rate) const {
        double next_time = get_updated_time(playback_rate);
        ssize_t search_idx = get_search_block_index(next_time);
        
        ssize_t target_needed = target_block_index + (ssize_t)ola_window_size - (ssize_t)input_buffer_frames();
        ssize_t search_needed = search_idx + (ssize_t)search_block_size - (ssize_t)input_buffer_frames();
        
        return std::max<ssize_t>({target_needed, search_needed, 0});
    }

    bool can_perform_wsola(float playback_rate) const {
        return frames_needed(playback_rate) <= 0;
    }

    void add_input_buffer_final_silence(float playback_rate) {
        ssize_t needed = frames_needed(playback_rate);
        if (needed <= 0) {
            return;
        }

        size_t needed_usize = (size_t)needed;
        for (size_t ch = 0; ch < channels; ++ch) {
            size_t len = input_buffer[ch].size();
            input_buffer[ch].resize(len + needed_usize, 0.0f);
        }
        input_buffer_added_silence += needed_usize;
    }

    bool run_one_wsola_iteration(float playback_rate) {
        if (!can_perform_wsola(playback_rate)) {
            return false;
        }

        double next_output_time = output_time + (double)ola_hop_size * (double)playback_rate;
        output_time = next_output_time;
        search_block_index = (ssize_t)std::floor(next_output_time - (double)search_block_center_offset + 0.5);

        remove_old_input_frames();

        assert(search_block_index + (ssize_t)search_block_size <= (ssize_t)input_buffer_frames());

        get_optimal_block();

        for (size_t k = 0; k < channels; ++k) {
            if (wsola_output_started) {
                for (size_t n = 0; n < ola_hop_size; ++n) {
                    size_t out_idx = num_complete_frames + n;
                    wsola_output[k][out_idx] = wsola_output[k][out_idx] * ola_window[ola_hop_size + n]
                        + optimal_block[k][n] * ola_window[n];
                }
                size_t dest_start = num_complete_frames + ola_hop_size;
                std::copy(
                    optimal_block[k].begin() + ola_hop_size,
                    optimal_block[k].begin() + ola_window_size,
                    wsola_output[k].begin() + dest_start
                );
            } else {
                std::copy(
                    optimal_block[k].begin(),
                    optimal_block[k].begin() + ola_window_size,
                    wsola_output[k].begin() + num_complete_frames
                );
            }
        }

        num_complete_frames += ola_hop_size;
        wsola_output_started = true;
        return true;
    }

    void remove_old_input_frames() {
        ssize_t earliest_used_index = std::min(target_block_index, search_block_index);
        if (earliest_used_index <= 0) {
            return;
        }

        size_t frames = (size_t)earliest_used_index;
        seek_buffer(frames);
        target_block_index -= earliest_used_index;
        output_time -= (double)earliest_used_index;
        search_block_index -= earliest_used_index;
    }

    size_t write_completed_frames_to(size_t requested_frames, float* const* dest, size_t dest_offset) {
        size_t rendered_frames = std::min(num_complete_frames, requested_frames);
        if (rendered_frames == 0) {
            return 0;
        }

        for (size_t ch = 0; ch < channels; ++ch) {
            for (size_t f = 0; f < rendered_frames; ++f) {
                dest[ch][dest_offset + f] = wsola_output[ch][f];
            }
            wsola_output[ch].erase(wsola_output[ch].begin(), wsola_output[ch].begin() + rendered_frames);
            wsola_output[ch].resize(wsola_output_size, 0.0f);
        }

        num_complete_frames -= rendered_frames;
        return rendered_frames;
    }

    size_t read_input_buffer(size_t dest_size, float* const* dest) {
        size_t target_idx = (size_t)std::max<ssize_t>(target_block_index, 0);
        size_t frames_avail = (input_buffer_frames() >= target_idx) ? (input_buffer_frames() - target_idx) : 0;
        size_t frames_to_copy = std::min(dest_size, frames_avail);
        if (frames_to_copy == 0) {
            return 0;
        }

        for (size_t i = 0; i < channels; ++i) {
            std::copy(
                input_buffer[i].begin() + target_idx,
                input_buffer[i].begin() + target_idx + frames_to_copy,
                dest[i]
            );
        }
        seek_buffer(frames_to_copy);
        return frames_to_copy;
    }

    size_t fill_buffer(float* const* dest, size_t dest_size, float playback_rate) {
        if (playback_rate == 0.0f) {
            return 0;
        }

        if (input_buffer_final_frames > 0) {
            add_input_buffer_final_silence(playback_rate);
        }

        if (playback_rate < min_playback_rate
            || (max_playback_rate > 0.0f && playback_rate > max_playback_rate))
        {
            size_t frames_to_render = std::min(
                dest_size,
                (size_t)((float)input_buffer_frames() / playback_rate)
            );

            muted_partial_frame += (double)frames_to_render * (double)playback_rate;
            size_t seek_frames = (size_t)std::floor(muted_partial_frame);
            
            for (size_t ch = 0; ch < channels; ++ch) {
                std::fill(dest[ch], dest[ch] + frames_to_render, 0.0f);
            }
            seek_buffer(seek_frames);
            muted_partial_frame -= (double)seek_frames;
            return frames_to_render;
        }

        size_t slower_step = (size_t)std::ceil((float)ola_window_size * playback_rate);
        size_t faster_step = (size_t)std::ceil((float)ola_window_size / playback_rate);

        if (ola_window_size <= faster_step && slower_step >= ola_window_size) {
            if (wsola_output_started) {
                wsola_output_started = false;
                ssize_t sync_time = target_block_index;
                output_time = (double)sync_time;
                search_block_index = get_search_block_index(output_time);
                remove_old_input_frames();
            }

            return read_input_buffer(dest_size, dest);
        }

        size_t rendered_frames = 0;
        while (true) {
            size_t wrote = write_completed_frames_to(dest_size - rendered_frames, dest, rendered_frames);
            rendered_frames += wrote;
            
            if (rendered_frames >= dest_size) {
                break;
            }

            if (!run_one_wsola_iteration(playback_rate)) {
                break;
            }
        }
        return rendered_frames;
    }

    void append_input(float* const* src, size_t src_offset, size_t frames) {
        if (frames == 0) return;
        for (size_t ch = 0; ch < channels; ++ch) {
            input_buffer[ch].insert(input_buffer[ch].end(), src[ch] + src_offset, src[ch] + src_offset + frames);
        }
    }
};

} // namespace sfb
