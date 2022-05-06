// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_CONSTANTS_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_CONSTANTS_H_

#include <stdint.h>

#include <ffl/fixed.h>
#include <ffl/string.h>

namespace media::audio {

// The number of fractional bits used when expressing timestamps (in frame
// units) as fixed point integers.  Ultimately, this determines the resolution
// that a source of PCM frames may be sampled at; there are 2^frac_bits
// positions between audio frames that the source stream may be sampled at.
//
// Using 64-bit signed timestamps means that we have 50 bits of whole frame
// units to work with.  At 192KHz, this allows for ~186.3 years of usable range
// before rollover when starting from a frame counter of 0.
//
// With 13 bits of fractional position, a mix job's interpolation precision is
// only +/-61 ppm. Across multiple jobs we stay in sync, but for any single mix,
// this is our granularity. As an example, when resampling a 48 kHz audio
// packet, the "clicks on the dial" of our actual resampling rates are multiples
// of 6 Hz. Again, we do correct any positional error at mix job boundaries.
//
// This also affects our interpolation accuracy: because fractional position has
// a potential error of 2^-13, the worst-case error for interpolated values is
// [pos_err * max_intersample_delta]. This means full-scale very high-frequency
// signals are only guaranteed bit-for-bit accurate in the top 13 bits.
// TODO(mpuryear): fxbug.dev/13372 Consider even more fractional position precision.
constexpr int32_t kPtsFractionalBits = 13;
// Used in places where PTS must be an integral number of frames.
constexpr int64_t kPtsFractionalMask = (1 << kPtsFractionalBits) - 1;

// Type to use for frame numbers.
using Fixed = ffl::Fixed<int64_t, kPtsFractionalBits>;
static constexpr Fixed kOneFrame = Fixed(1);
static constexpr Fixed kHalfFrame = ffl::FromRatio(1, 2);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_CONSTANTS_H_
