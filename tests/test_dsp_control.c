// Regression coverage for silent amp controls and fixed-point DSP boundaries.
#include "amy.h"
#include <stdio.h>

extern float amp_combine_controls(float *controls, float *coefs);

static int check_amplitude(void) {
    const float gains[] = {0, 0.01f, 0.1f, 0.5f, 1, 2};
    const float inputs[] = {0, 0.02f, 0.2f, 0.7f, 1};
    unsigned checks = 0;
    for (unsigned g = 0; g < sizeof(gains) / sizeof(*gains); ++g)
    for (unsigned v = 0; v < sizeof(inputs) / sizeof(*inputs); ++v)
    for (unsigned e = 0; e < sizeof(inputs) / sizeof(*inputs); ++e) {
        float controls[NUM_COMBO_COEFS] = {0}, coefs[NUM_COMBO_COEFS] = {0};
        controls[COEF_CONST] = 1;
        controls[COEF_VEL] = inputs[v];
        controls[COEF_EG0] = inputs[e];
        controls[COEF_MOD0] = 0.3f;
        controls[COEF_MOD1] = -0.1f;
        coefs[COEF_CONST] = gains[g];
        coefs[COEF_VEL] = coefs[COEF_EG0] = 1;
        coefs[COEF_MOD0] = 0.1f;
        coefs[COEF_MOD1] = -0.2f;
        float expected = gains[g] * inputs[v] * inputs[e] * powf(10, 0.15f);
        if (expected <= AMP_THRESH_PLUS) expected = 0;
        float actual = amp_combine_controls(controls, coefs);
        if (!isfinite(actual) || fabsf(actual - expected) > 0.0001f + 0.01f * expected ||
            (gains[g] == 0 && actual != 0)) {
            fprintf(stderr, "amplitude mismatch: level %g velocity %g envelope %g: %g != %g\n",
                gains[g], inputs[v], inputs[e], actual, expected);
            return 1;
        }
        ++checks;
    }
    float controls[NUM_COMBO_COEFS] = {0}, coefs[NUM_COMBO_COEFS] = {0};
    coefs[COEF_CONST] = coefs[COEF_MOD0] = 1;
    for (int i = -100; i <= 100; ++i) {
        controls[COEF_MOD0] = i / 10.0f;
        float actual = amp_combine_controls(controls, coefs);
        if (!isfinite(actual) || actual < 0 || actual >= 256) return 1;
    }
    controls[COEF_MOD0] = NAN;
    if (amp_combine_controls(controls, coefs) != 0) return 1;
    printf("amplitude: %u gain/velocity/envelope comparisons and wide modulation bounds passed\n", checks);
    return 0;
}

static int check_envelope_bounds(void) {
    struct synthinfo oscillator = {0};
    struct synthinfo *pointer = &oscillator;
    synth = &pointer;
    uint32_t times[] = {1, 0, 0};
    float levels[] = {1, 0.0002f, 0};
    oscillator.breakpoint_times[0] = times;
    oscillator.breakpoint_values[0] = levels;
    oscillator.max_num_breakpoints[0] = 3;
    const uint32_t durations[] = {2, 17, 256000, 441000, 5292000};
    for (int type = 0; type < 2; ++type)
    for (int release = 0; release < 2; ++release)
    for (int rise = 0; rise <= type; ++rise)
    for (unsigned d = 0; d < sizeof(durations) / sizeof(*durations); ++d) {
        oscillator.eg_type[0] = type ? ENVELOPE_TRUE_EXPONENTIAL : ENVELOPE_DX7;
        times[1] = times[2] = durations[d];
        levels[0] = rise ? 0.0002f : 1.0f;
        levels[1] = levels[2] = rise ? 1.0f : 0.0002f;
        oscillator.note_on_clock = 0;
        AMY_UNSET(oscillator.note_off_clock);
        if (release) {
            AMY_UNSET(oscillator.note_on_clock);
            oscillator.note_off_clock = 0;
            oscillator.last_scale[0] = F2S(levels[0]);
        }
        SAMPLE previous = F2S(levels[0]);
        uint32_t step = MAX(1, durations[d] / 113);
        for (uint32_t position = 0; position < durations[d]; position += step) {
            uint32_t clock = position;
            amy_global.total_blocks = clock / AMY_BLOCK_SIZE;
            uint16_t offset = clock % AMY_BLOCK_SIZE;
            SAMPLE actual = compute_breakpoint_scale(0, 0, offset);
            float ratio = (float)position / durations[d];
            float expected = rise ? powf(0.0002f, 1.0f - ratio) : powf(0.0002f, ratio);
            if (actual < 0 || (rise ? actual < previous : actual > previous) ||
                fabsf(S2F(actual) - expected) > 0.0005f + 0.005f * expected) {
                fprintf(stderr, "unbounded/discontinuous envelope: type %d release %d duration %u position %u value %g expected %g\n",
                    type, release, durations[d], position, S2F(actual), expected);
                return 1;
            }
            previous = actual;
        }
    }
    // Two endpoints below the attack-map floor used to generate NaN.
    oscillator.eg_type[0] = ENVELOPE_DX7;
    oscillator.note_on_clock = 0;
    AMY_UNSET(oscillator.note_off_clock);
    times[1] = 4410;
    levels[0] = 0;
    levels[1] = 0.001f;
    SAMPLE previous = 0;
    for (uint32_t clock = 0; clock < times[1]; ++clock) {
        amy_global.total_blocks = clock / AMY_BLOCK_SIZE;
        SAMPLE actual = compute_breakpoint_scale(0, 0, clock % AMY_BLOCK_SIZE);
        if (actual < previous || actual > F2S(levels[1])) return 1;
        previous = actual;
    }
    synth = NULL;
    puts("envelope bounds: fast decays, 120-second decays/releases and below-floor attacks passed");
    return 0;
}

static int check_fm_bounds(void) {
    const int16_t table[] = {0, 32767, 0, -32767};
    const LUT lut = {table, 4, 2, 1, 1};
    struct synthinfo oscillator = {0};
    struct mod_synthinfo mod = {0};
    struct synthinfo *pointer = &oscillator;
    struct mod_synthinfo *mod_pointer = &mod;
    synth = &pointer;
    msynth = &mod_pointer;
    oscillator.lut = &lut;
    AMY_UNSET(oscillator.logratio);
    mod.amp = mod.last_amp = 0.5f;
    const float frequencies[] = {440, AMY_SAMPLE_RATE * 0.5f, AMY_SAMPLE_RATE,
        AMY_SAMPLE_RATE * 1.5f, AMY_SAMPLE_RATE * 31.125f};
    for (unsigned f = 0; f < sizeof(frequencies) / sizeof(*frequencies); ++f) {
        mod.logfreq = logfreq_of_freq(frequencies[f]);
        float step = freq_of_logfreq(mod.logfreq) / AMY_SAMPLE_RATE;
        step -= floorf(step);
        uint32_t increment = (uint32_t)(step * 2147483648.0);
        uint32_t expected = (increment * AMY_BLOCK_SIZE) & 0x7fffffff;
        oscillator.phase = 0;
        SAMPLE output[AMY_BLOCK_SIZE] = {0};
        render_fm_sine(output, 0, NULL, 0, 0, F2S(1.0f));
        if (((uint32_t)oscillator.phase & 0x7fffffff) != expected) {
            fprintf(stderr, "FM phase mismatch: freq %g actual %u expected %u\n", frequencies[f], (uint32_t)oscillator.phase, expected);
            return 1;
        }
    }
    const float amplitudes[] = {-1, 0, 0.0002f, 1, 31, 128, 255.999f, 256, 260, 1e20f, INFINITY, NAN};
    for (unsigned a = 0; a < sizeof(amplitudes) / sizeof(*amplitudes); ++a) {
        SAMPLE converted = amy_amp_to_sample(amplitudes[a]);
        if (converted < 0) { fprintf(stderr, "negative FM level for %g\n", amplitudes[a]); return 1; }
        if (amplitudes[a] >= 0 && amplitudes[a] < 256 && converted != F2S(amplitudes[a])) {
            fprintf(stderr, "changed in-range FM level %g\n", amplitudes[a]); return 1;
        }
        mod.amp = mod.last_amp = amplitudes[a];
        SAMPLE output[AMY_BLOCK_SIZE] = {0};
        render_fm_sine(output, 0, NULL, 0, 0, F2S(1.0f));
    }
    synth = NULL;
    msynth = NULL;
    puts("FM bounds: high ratios and excessive/nonfinite levels passed");
    return 0;
}

static int check_silent_chain(void) {
    amy_config_t config = amy_default_config();
    config.max_oscs = 4;
    config.max_voices = config.max_synths = 2;
    config.max_buses = 1;
    config.max_sequencer_tags = 1;
    config.platform.multicore = config.platform.multithread = 0;
    config.features.chorus = config.features.echo = config.features.reverb = 0;
    config.features.partials = config.features.custom = 0;
    amy_start(config);
    amy_event event = amy_default_event();
    event.osc = 1;
    event.wave = SINE;
    event.freq_coefs[COEF_CONST] = 440;
    amy_add_event(&event);
    event = amy_default_event();
    event.osc = 0;
    event.wave = SILENT;
    event.chained_osc = 1;
    event.velocity = 1;
    amy_add_event(&event);
    int audible = 0;
    for (int block = 0; block < 4; ++block) {
        const output_sample_type *pcm = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i) audible |= pcm[i] != 0;
    }
    if (!audible) { fputs("chain fixture is silent\n", stderr); return 1; }
    event = amy_default_event();
    event.osc = 0;
    event.amp_coefs[COEF_CONST] = 0;
    amy_add_event(&event);
    const output_sample_type *pcm = NULL;
    for (int block = 0; block < 4; ++block) pcm = amy_simple_fill_buffer();
    for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)
        if (pcm[i] != 0) { fputs("zero-level chain still audible\n", stderr); return 1; }
    event.amp_coefs[COEF_CONST] = 1;
    amy_add_event(&event);
    audible = 0;
    for (int block = 0; block < 4; ++block) {
        pcm = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i) audible |= pcm[i] != 0;
    }
    if (!audible) { fputs("chain did not recover its level\n", stderr); return 1; }
    amy_stop();
    puts("silent chain: zero level mutes a held note and restores it passed");
    return 0;
}

int main(void) {
    if (check_amplitude() || check_envelope_bounds() || check_fm_bounds()) return 1;
    for (int i = -32768; i < 32768; ++i) {
        float phase = i / 65536.0f;
        PHASOR expected = (PHASOR)(phase * 2.0 * (float)(1 << (P_FRAC_BITS - 1)));
        if (F2P(phase) != expected) return 1;
    }
    puts("phase: 65536 comparisons passed");
    return check_silent_chain();
}
