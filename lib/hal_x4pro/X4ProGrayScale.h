#pragma once

/**
 * @file X4ProGrayScale.h
 * @brief Speed-scaled variant of the OEM UC8279d 4-level grayscale bank.
 *
 * kUc8279X3_Xth4 is 71 frames. That is slow and it flashes, because the waveform swings
 * hard between VDL and VDH several times per channel. Scaling every phase duration by a
 * common factor shortens it while preserving the two properties that make the bank safe:
 *
 *   - DC balance. Each table's net charge is 0; scaling is linear, so 0 stays 0.
 *   - Lockstep. All five tables span the same frame count; scaling keeps them equal.
 *
 * Rounding can break both (a phase must never round to 0 frames, and rounding is not
 * exactly linear), so after scaling the result is explicitly rebalanced back to net 0 and
 * padded with a GND group so every table matches the longest.
 *
 * Format: 7-byte groups; bytes 1..4 are phases encoded [2-bit rail][6-bit frames]
 * (00=GND 01=VDH 10=VDL 11=VDHR); byte 0 and the trailing two are structural (0x01). A
 * group whose four phase bytes are all zero terminates the waveform.
 *
 * Tune with -DX4PRO_GRAY_SPEED=<percent> (100 = the original 71 frames):
 *      60  -> 47 frames   (default; ~34% faster, noticeably less flashing)
 *      50  -> 44 frames
 *      40  -> 33 frames   (fastest; expect softer blacks)
 * Below ~40 the short phases collapse to their 1-frame floor and tone separation suffers.
 */

#include <Arduino.h>

namespace inx {
namespace x4pro {

/** The scaled bank, built once on first use. Returns 5 tables of 49 bytes. */
const uint8_t (*scaledGrayBank())[49];

}  // namespace x4pro
}  // namespace inx
