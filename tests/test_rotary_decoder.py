"""Host regressions for the production ISR-facing rotary quadrature decoder."""

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
#include <stdint.h>

static void sample(rotary_decoder_t *decoder, uint8_t a, uint8_t b)
{
    rotary_decoder_sample(decoder, a);
    rotary_decoder_sample(decoder, b);
}

int main(void)
{
    rotary_decoder_t decoder = { 0 };
    rotary_decoder_reset(&decoder, 0);
    assert(!rotary_decoder_pending(&decoder));

    /* Two half-steps in each clean full Gray-code cycle, preserving original detent calibration. */
    sample(&decoder, 1, 3);
    sample(&decoder, 2, 0);
    assert(rotary_decoder_take_step(&decoder) == -1);
    assert(rotary_decoder_take_step(&decoder) == -1);
    assert(rotary_decoder_take_step(&decoder) == 0);
    sample(&decoder, 2, 3);
    sample(&decoder, 1, 0);
    assert(rotary_decoder_take_step(&decoder) == 1);
    assert(rotary_decoder_take_step(&decoder) == 1);

    /* Bounce back to the same latch and duplicate IRQs must not create steps. */
    sample(&decoder, 1, 0);
    sample(&decoder, 1, 0);
    rotary_decoder_sample(&decoder, 0);
    assert(!rotary_decoder_pending(&decoder));
    sample(&decoder, 1, 3);
    sample(&decoder, 1, 3);
    assert(rotary_decoder_take_step(&decoder) == -1);
    assert(rotary_decoder_take_step(&decoder) == 0);
    sample(&decoder, 2, 0);
    assert(rotary_decoder_take_step(&decoder) == -1);

    /* A skipped A/B edge (00 -> 11) does not invent or reverse a detent. */
    rotary_decoder_sample(&decoder, 3);
    assert(decoder.invalid_transitions == 1);
    assert(!rotary_decoder_pending(&decoder));
    sample(&decoder, 2, 0);
    assert(rotary_decoder_take_step(&decoder) == -1);

    /* Preserve a forward and immediate backward step independently (no signed cancellation). */
    sample(&decoder, 2, 3);
    sample(&decoder, 2, 0);
    assert(rotary_decoder_take_step(&decoder) == 1);
    assert(rotary_decoder_take_step(&decoder) == -1);

    /* Wake or input-lock clears unfinished turns and queued movement. */
    sample(&decoder, 1, 3);
    rotary_decoder_sample(&decoder, 2);
    rotary_decoder_clear_pending(&decoder);
    rotary_decoder_sample(&decoder, 0);
    assert(!rotary_decoder_pending(&decoder));
    sample(&decoder, 1, 3);
    assert(rotary_decoder_take_step(&decoder) == -1);
    assert(!rotary_decoder_pending(&decoder));

    /* Startup at a non-latch phase must wait for a stable latch before emitting movement. */
    rotary_decoder_reset(&decoder, 1);
    rotary_decoder_sample(&decoder, 3);
    assert(!rotary_decoder_pending(&decoder));
    sample(&decoder, 2, 0);
    assert(rotary_decoder_take_step(&decoder) == -1);

    /* Bounded memory: a long task stall drops newer steps but never corrupts the ring. */
    rotary_decoder_reset(&decoder, 0);
    for(unsigned i = 0; i < 33; ++i)
    {
        sample(&decoder, 1, 3);
        sample(&decoder, 2, 0);
    }
    assert(decoder.count == ROTARY_DECODER_QUEUE_CAPACITY);
    assert(decoder.dropped_steps == 2);
    for(unsigned i = 0; i < ROTARY_DECODER_QUEUE_CAPACITY; ++i)
        assert(rotary_decoder_take_step(&decoder) == -1);
    assert(!rotary_decoder_pending(&decoder));
    sample(&decoder, 2, 3);
    assert(rotary_decoder_take_step(&decoder) == 1);
    return 0;
}
'''


class RotaryDecoderTests(unittest.TestCase):
    def test_quadrature_and_ordered_navigation(self):
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
