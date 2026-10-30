"""Host regressions for RFScout's LilyGO TWO03-compatible software decoder."""

import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
INPUT = ROOT / "main/services/input"

HARNESS = r'''
#include "rotary_decoder.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Independent model of Matthias Hertel RotaryEncoder::tick() / TWO03. */
static const int8_t reference_steps[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
};

typedef struct {
    uint8_t state;
    int32_t position;
    int32_t published;
    uint32_t invalid;
} reference_t;

static void reference_tick(reference_t *reference, uint8_t current)
{
    if(reference->state == current)
    {
        return;
    }
    if((reference->state ^ current) == 3U)
    {
        ++reference->invalid;
    }
    reference->position += reference_steps[(reference->state << 2U) | current];
    reference->state = current;
    if(current == 0U || current == 3U)
    {
        reference->published = reference->position >> 1;
    }
}

static void verify_trace(const uint8_t *states, size_t count)
{
    assert(count > 0);
    rotary_decoder_t decoder = { 0 };
    reference_t ref = { .state = states[0] };
    rotary_decoder_reset(&decoder, states[0]);
    for(size_t i = 1; i < count; ++i)
    {
        rotary_decoder_sample(&decoder, states[i]);
        reference_tick(&ref, states[i]);
        assert(decoder.last_state == ref.state);
        assert(decoder.position == ref.position);
        assert(decoder.latched_position == ref.published);
        assert(decoder.invalid_transitions == ref.invalid);
    }
}

int main(void)
{
    const uint8_t forward[] = { 0, 1, 3, 2, 0, 1, 3, 2, 0 };
    const uint8_t reverse[] = { 0, 2, 3, 1, 0, 2, 3, 1, 0 };
    const uint8_t bounce[] = { 0, 1, 0, 1, 3, 1, 3, 2, 3, 2, 0 };
    const uint8_t invalid[] = { 0, 3, 2, 0, 3, 1, 0 };
    const uint8_t mid_latch[] = { 1, 3, 2, 0, 2, 3 };
    verify_trace(forward, sizeof(forward));
    verify_trace(reverse, sizeof(reverse));
    verify_trace(bounce, sizeof(bounce));
    verify_trace(invalid, sizeof(invalid));
    verify_trace(mid_latch, sizeof(mid_latch));

    rotary_decoder_t decoder = { 0 };
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1); /* A edge, not a TWO03 latch. */
    assert(!rotary_decoder_pending(&decoder));
    assert(rotary_decoder_take_direction(&decoder) == 0);
    rotary_decoder_sample(&decoder, 3);
    assert(decoder.position == -2);
    assert(decoder.latched_position == -1);
    assert(rotary_decoder_take_direction(&decoder) == 1);
    rotary_decoder_sample(&decoder, 2);
    assert(!rotary_decoder_pending(&decoder));
    rotary_decoder_sample(&decoder, 0);
    assert(decoder.position == -4); /* Cumulative across latches; never reset. */
    assert(rotary_decoder_take_direction(&decoder) == 1);

    /* Mechanical rebound along an edge and duplicated interrupts cancel. */
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 0);
    rotary_decoder_sample(&decoder, 0);
    assert(!rotary_decoder_pending(&decoder));
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    assert(rotary_decoder_take_direction(&decoder) == 1);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    assert(!rotary_decoder_pending(&decoder));

    /* Physical reversals retain their sign after mapping to RFScout navigation. */
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 0);
    assert(rotary_decoder_take_direction(&decoder) == -1);

    /* Intermediate states do not wake the UI until a complete latch is seen. */
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1);
    assert(!rotary_decoder_pending(&decoder));
    rotary_decoder_sample(&decoder, 3);
    assert(rotary_decoder_pending(&decoder));

    /* A busy UI receives only one net navigation event, never queued detents. */
    rotary_decoder_reset(&decoder, 0);
    for(unsigned i = 0; i < 34; ++i)
    {
        rotary_decoder_sample(&decoder, 1);
        rotary_decoder_sample(&decoder, 3);
        rotary_decoder_sample(&decoder, 2);
        rotary_decoder_sample(&decoder, 0);
    }
    assert(decoder.latched_position == -68);
    assert(rotary_decoder_take_direction(&decoder) == 1);
    assert(!rotary_decoder_pending(&decoder));
    assert(rotary_decoder_take_direction(&decoder) == 0);

    /* Opposite complete rotations cancel while unconsumed. */
    rotary_decoder_reset(&decoder, 0);
    const uint8_t back_forth[] = { 1, 3, 2, 0, 2, 3, 1, 0 };
    for(size_t i = 0; i < sizeof(back_forth); ++i)
    {
        rotary_decoder_sample(&decoder, back_forth[i]);
    }
    assert(decoder.latched_position == 0);
    assert(!rotary_decoder_pending(&decoder));

    /* Wake/lock clears completed and partial motion; diagnostics persist. */
    rotary_decoder_sample(&decoder, 3);
    assert(decoder.invalid_transitions == 1);
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    rotary_decoder_sample(&decoder, 2);
    rotary_decoder_clear_pending(&decoder);
    assert(!rotary_decoder_pending(&decoder));
    assert(decoder.position == 0);
    assert(decoder.last_state == 2);
    rotary_decoder_sample(&decoder, 3);
    rotary_decoder_sample(&decoder, 2);
    assert(!rotary_decoder_pending(&decoder));

    /* Codex P2: a clear at 10 must discard the final 10 -> 00 edge.
       00 -> 01 -> 11 -> 10 -> clear -> 00 cannot emit navigation. */
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    rotary_decoder_sample(&decoder, 2);
    assert(rotary_decoder_pending(&decoder));
    rotary_decoder_clear_pending(&decoder);
    assert(decoder.suppress_until_latch);
    assert(!rotary_decoder_pending(&decoder));
    rotary_decoder_sample(&decoder, 0);
    assert(!decoder.suppress_until_latch);
    assert(decoder.position == 0);
    assert(decoder.latched_position == 0);
    assert(rotary_decoder_take_direction(&decoder) == 0);
    /* After resynchronization, a complete NEW half-detent is accepted. */
    rotary_decoder_sample(&decoder, 1);
    assert(!rotary_decoder_pending(&decoder));
    rotary_decoder_sample(&decoder, 3);
    assert(rotary_decoder_take_direction(&decoder) == 1);

    /* Symmetric case: clear at 01, then 01 -> 00 must not leak a step. */
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 2);
    rotary_decoder_sample(&decoder, 3);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_clear_pending(&decoder);
    assert(decoder.suppress_until_latch);
    rotary_decoder_sample(&decoder, 0);
    assert(!rotary_decoder_pending(&decoder));
    assert(rotary_decoder_take_direction(&decoder) == 0);
    rotary_decoder_sample(&decoder, 2);
    rotary_decoder_sample(&decoder, 3);
    assert(rotary_decoder_take_direction(&decoder) == -1);

    /* No special suppression when clearing at a stable 00/11 latch. */
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    rotary_decoder_clear_pending(&decoder);
    assert(!decoder.suppress_until_latch);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_sample(&decoder, 0);
    assert(rotary_decoder_take_direction(&decoder) == -1);

    /* Repeated clearing mid-turn must not resume decoding prematurely. */
    rotary_decoder_reset(&decoder, 0);
    rotary_decoder_sample(&decoder, 1);
    rotary_decoder_clear_pending(&decoder);
    rotary_decoder_sample(&decoder, 2); /* Invalid diagonal while suppressed. */
    assert(decoder.invalid_transitions == 1);
    rotary_decoder_clear_pending(&decoder);
    assert(decoder.suppress_until_latch);
    rotary_decoder_sample(&decoder, 3);
    assert(!decoder.suppress_until_latch);
    assert(!rotary_decoder_pending(&decoder));
    rotary_decoder_sample(&decoder, 2);
    rotary_decoder_sample(&decoder, 0);
    assert(rotary_decoder_take_direction(&decoder) == 1);

    /* LilyGO starts between latches with raw position zero, not phase-biased. */
    rotary_decoder_reset(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    assert(decoder.latched_position == -1);
    return 0;
}
'''


class RotaryDecoderTests(unittest.TestCase):
    def test_lilygo_state_ordering_and_periodic_sampling(self):
        source = (INPUT / "input.c").read_text()
        self.assertIn("return a | (uint8_t)( b << 1U );", source)
        self.assertIn("INPUT_ROTARY_SAMPLE_PERIOD_US 1000U", source)
        self.assertIn("esp_timer_start_periodic( encoder_timer, INPUT_ROTARY_SAMPLE_PERIOD_US )", source)
        self.assertIn("rotary_decoder_sample( &navigation_decoder, read_encoder_state() )", source)
        self.assertNotIn("gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_A_PIN", source)
        self.assertNotIn("gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_B_PIN", source)
        self.assertIn("gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_KEY_PIN", source)
        self.assertIn("rotary_decoder_pending( &navigation_decoder )", source)
        self.assertNotIn("pcnt_new_unit", source)

    def test_reference_traces_latches_and_coalescing(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            temp = pathlib.Path(folder)
            source = temp / "rotary_harness.c"
            source.write_text(HARNESS)
            binary = temp / "rotary_harness"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(INPUT), str(source), str(INPUT / "rotary_decoder.c"),
                 "-o", str(binary)], check=True
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
