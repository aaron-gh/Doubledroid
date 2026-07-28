// license:BSD-3-Clause
// copyright-holders:David Sexton
// C API implementation over doubletalk_board. See dtalk.h.

#define DTALK_BUILD 1
#include "dtalk.h"

#include "doubletalk_board.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <vector>

namespace {

// Firmware timer cadence: one DAC write per 952 CPU cycles (measured).
constexpr u32 SAMPLE_RATE = doubletalk_board::CPU_HZ / 952; // 10504

// ---- modeled output stage (dtalk_synth16) ----------------------------------
// Reproduces what the MAME ISA-card build does to the identical raw DAC bytes
// (the cleaner reference the user compares against), not real analog hardware:
//
//   * MAME routes its 8-bit R-2R DAC at 0.5 (doubletalkpc.cpp:
//     add_route(ALL_OUTPUTS, "mono", 0.5)); measured, this is exactly the raw
//     0.703 Big-Bob peak scaled to MAME's 0.352 peak, so 0.5 is used verbatim
//     as the headroom gain - it takes the loud presets off the rails without
//     making voice 0 quiet, and matches the reference level bit for bit.
//   * MAME's sound_stream resamples the 10504Hz zero-order-hold stream up to
//     the host rate through a band-limiting filter, which smooths the DAC
//     staircase (the source of the "hiss while speaking"). The 2-pole
//     Butterworth low-pass below plays that role inside the 10504Hz stream.
//   * MAME's stream is continuous - no per-utterance restarts - so it never
//     emits the DC-step click our per-request path can. The DC-blocking
//     high-pass removes the standing DC offset that causes those clicks.
constexpr double DT_PI = 3.14159265358979323846; // DT_PI is not portable (mingw -std=c++20)
constexpr double DC_BLOCK_HZ = 20.0;    // one-pole high-pass corner
constexpr double LPF_HZ = 3000.0;       // default reconstruction low-pass corner.
                                        // Chosen by measurement: matching the
                                        // MAME reference's spectral tilt (its
                                        // resampler drops the raw HF-energy
                                        // ratio from 0.127 to 0.096) needs a
                                        // ~2.8-3.0kHz corner. This coincides
                                        // with the RC8650/8660 datasheets' own
                                        // recommended output filter, captioned
                                        // "Low Cost 3 kHz Low-Pass Filter" - so
                                        // MAME and the real card's spec agree.
constexpr double HEADROOM_GAIN = 0.5;   // MAME's DAC output route
                                        // (add_route(ALL_OUTPUTS,"mono",0.5))
// User-selectable low-pass corner bounds (dtalk_set_lowpass_hz). 3000 is the
// authentic datasheet default. The NVDA driver's "Wide" preset uses 4800, but
// that sits close enough to the ~5252Hz Nyquist limit that the 2-pole filter
// barely attenuates the 10.5kHz DAC's staircase noise below it (measured:
// -0.06dB at 4000Hz, -5.7dB at 4900Hz), so the artifacting it was meant to
// tame stays audible. This app's own "Wide" preset uses 4000 instead - still
// clearly brighter than Classic but with ~20-30dB more attenuation across the
// noise band.
constexpr double LPF_HZ_MIN = 500.0;
constexpr double LPF_HZ_MAX = 5000.0;

// ---- pause shortening (dtalk_set_pause_cap_ms) -----------------------------
// The firmware itself has no command that shortens the silence after
// sentence-ending punctuation (the manual's nT "word gap" already defaults to
// 0, its fastest setting - there's nothing faster to ask for). What users
// actually perceive as a long pause is the firmware's own end-of-sentence/
// comma intonation drop, which just holds the DAC at its idle level
// (m_dac_level's power-on value, doubletalk_board.h) for a while. So instead
// of a firmware command, this trims that silence directly in the generated
// PCM: any run of raw samples within PAUSE_SILENCE_TOL of the idle byte
// (0x80, matching doubletalk_board's m_dac_level reset value) longer than the
// configured cap is truncated to the cap. Word-to-word gaps (governed by nT,
// already at its authentic minimum) are far shorter than any sane cap here,
// so only the long punctuation/sentence pauses are ever affected.
constexpr u8 PAUSE_IDLE_BYTE = 0x80;
constexpr int PAUSE_SILENCE_TOL = 1;

// An utterance's lead-in silence (the firmware still parsing the command
// prefix/text before its very first sound) is always capped to this, no
// matter what pause_cap_samples the host has configured for mid-utterance
// pauses - a slow start isn't something anyone wants to opt out of the way
// they might want the authentic sentence-pause length, and this is cheap:
// see the trimming loop in pull() for why it's a net win, not just a
// tradeoff.
constexpr u32 LEADIN_CAP_SAMPLES = SAMPLE_RATE * 10 / 1000; // 10ms

// Firmware text-buffer read/write pointers in CPU RAM (same addresses the
// MAME capture.lua watched): equal <=> input buffer fully consumed.
constexpr u32 BUF_READ_PTR = 0x000f;
constexpr u32 BUF_WRITE_PTR = 0x0011;

// Idle must hold this long (emulated) before we call the stream finished -
// covers the firmware's own latency between consuming text and raising
// SYNC when audio starts.
constexpr s64 IDLE_SETTLE_CYCLES = doubletalk_board::CPU_HZ * 3 / 20; // 150ms

constexpr s64 RUN_CHUNK_CYCLES = doubletalk_board::CPU_HZ / 100; // 10ms

// ---- rate boost (dtalk_set_rate_boost) -------------------------------------
// The firmware's speech rate (nS 0-9) is driven by a table of per-frame
// "period" words in ROM. Empirically (see notes), the speed setting d selects
// entry (10 + d) of the word table at ROM offset RATE_TABLE_OFF; the value is
// the frame period - smaller = faster speech, and it does NOT change pitch
// (verified F0-stable). The firmware's own consumer clamps the table index to
// [0,22], and the table already extends past the speed-9 slot (idx 19) to
// even smaller periods (idx 20-22), so shorter periods are values the firmware
// is built to handle. The parser wraps any nS input > 9 back into 0-9 (a mod,
// documented as the RC8650 "SAT=0 wrap" behaviour), so >9S cannot be reached
// through the command interface - hence we rate-boost by rescaling the ROM
// period table's rate region in our private in-RAM ROM copy (never the file).
//
// A boost LEVEL scales the whole rate region (idx 10-22) by a fixed percent so
// every nS 0-9 the host may still send stays monotonic and intelligible - only
// the mapping from nS to real duration gets faster. Level 0 is the authentic
// table. The percents are the verified-safe operating points (see PORTING /
// rate-boost verification): pitch stays within a couple percent and speech
// stays intelligible at every speed 0-9 down to level 3; below ~65% the
// firmware's frame engine floors and speech breaks up, so we do not expose it.
constexpr u32 RATE_TABLE_OFF = 0x48da;   // ROM offset of the period word table
constexpr int RATE_REGION_LO = 10;       // first rate-region index (speed 0)
constexpr int RATE_REGION_HI = 22;       // last (speed-9 slot is 19; 20-22 = extension)
// level -> percent of stock period. Index by level (0..RATE_BOOST_MAX).
// 100 = authentic. 88/80 are verified above the firmware's frame-duration
// floor at every nS 0-9 (below ~76% the fastest speeds saturate/garble - see
// the rate-boost verification), so both keep duration strictly monotonic in
// speed and preserve pitch (measured F0-identical across levels).
constexpr int RATE_BOOST_PCT[] = { 100, 88, 80 };
constexpr int RATE_BOOST_MAX = 2;

// Modeled analog output stage state (dtalk_synth16 only). Direct-Form-I
// biquad low-pass in series after a one-pole DC-blocking high-pass.
struct output_stage
{
	// DC-block one-pole high-pass: y = x - x1 + R*y1
	double r = 0.0;
	double hp_x1 = 0.0, hp_y1 = 0.0;
	// Butterworth low-pass biquad (Direct Form I)
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	double lp_x1 = 0, lp_x2 = 0, lp_y1 = 0, lp_y2 = 0;
	bool prime = true; // next input primes the DC-block so resume has no step

	// Trailing-tail gate: a word-final stop-consonant release (e.g. the "d"
	// in "good.") is a single sharp excitation whose filter ringing lingers
	// for ~100-150ms after the raw excitation itself has already gone quiet
	// (measured: still ~1-3% of full scale there, well below speech level
	// but not literally zero). That's inaudible on a neutral desktop output,
	// but audible as a trailing buzz through a phone speaker path with
	// loudness/dynamics enhancement (e.g. Samsung's), which brings quiet
	// tails back up. This is self-contained and independent of the board's
	// own idle detection (dtalk_synth's IDLE_SETTLE_CYCLES, which exists to
	// tolerate normal inter-word pauses and shouldn't be shortened): checked
	// directly against each raw pre-filter sample (not a smoothed envelope,
	// which would lag behind the firmware going quiet and never catch up
	// before the utterance ends) so it starts counting the instant that
	// input itself drops quiet, then once it's stayed quiet for
	// GATE_HOLD_SAMPLES, fades whatever the filters are still ringing with
	// down to silence over a further ~15ms. A sustained vowel/continuant
	// never gets this quiet, so it can't false-trigger mid-word; a genuine
	// inter-word pause is already supposed to be near-silent, so gating it
	// early is harmless either way.
	int gate_quiet = 0;
	double gate_gain = 1.0;
	static constexpr double GATE_THRESH = 0.02;   // relative to full-scale [-1,1] input
	static constexpr int GATE_HOLD_SAMPLES = 300; // ~28ms @ SAMPLE_RATE before the fade begins
	static constexpr double GATE_DECAY = 0.05;    // per-sample multiplicative decay once triggered

	output_stage()
	{
		r = 1.0 - (2.0 * DT_PI * DC_BLOCK_HZ / double(SAMPLE_RATE));
		set_lowpass(LPF_HZ);
	}

	// Recompute the 2-pole Butterworth low-pass biquad (bilinear transform)
	// for a new corner frequency, leaving the filter's sample history (and the
	// DC-block state) untouched so the swap is glitch-free mid-stream.
	void set_lowpass(double hz)
	{
		if (hz < LPF_HZ_MIN) hz = LPF_HZ_MIN;
		else if (hz > LPF_HZ_MAX) hz = LPF_HZ_MAX;
		const double w = std::tan(DT_PI * hz / double(SAMPLE_RATE));
		const double k = w * w;
		const double q = std::sqrt(2.0); // Butterworth: 1/Q = sqrt(2)
		const double norm = 1.0 / (1.0 + q * w + k);
		b0 = k * norm;
		b1 = 2.0 * b0;
		b2 = b0;
		a1 = 2.0 * (k - 1.0) * norm;
		a2 = (1.0 - q * w + k) * norm;
	}

	void reset(double level = 0.0)
	{
		hp_x1 = level;
		hp_y1 = 0.0;
		lp_x1 = lp_x2 = lp_y1 = lp_y2 = 0.0;
		prime = true;
		gate_quiet = 0;
		gate_gain = 1.0;
	}

	int16_t process(double x)
	{
		if (prime)
		{
			// Seed the high-pass with this level so the first sample yields no
			// step (avoids a re-click when speech resumes after a stop/boot).
			hp_x1 = x;
			hp_y1 = 0.0;
			prime = false;
		}
		// DC-blocking high-pass
		double hp = x - hp_x1 + r * hp_y1;
		hp_x1 = x;
		hp_y1 = hp;
		// Butterworth low-pass biquad
		double lp = b0 * hp + b1 * lp_x1 + b2 * lp_x2 - a1 * lp_y1 - a2 * lp_y2;
		lp_x2 = lp_x1; lp_x1 = hp;
		lp_y2 = lp_y1; lp_y1 = lp;
		// trailing-tail gate (see the field comment): checked directly against
		// the raw, pre-filter excitation sample so it reflects what the
		// firmware is actually feeding in right now, not the filters' own
		// ringing (a smoothed envelope here would lag behind the firmware
		// actually going quiet and never catch up before the utterance ends).
		if (std::fabs(x) < GATE_THRESH)
		{
			if (gate_quiet < GATE_HOLD_SAMPLES) gate_quiet++;
		}
		else
		{
			gate_quiet = 0;
			gate_gain = 1.0;
		}
		if (gate_quiet >= GATE_HOLD_SAMPLES)
		{
			gate_gain *= (1.0 - GATE_DECAY);
			if (gate_gain < 0.0) gate_gain = 0.0;
		}
		// headroom gain, clamp to int16
		double y = lp * gate_gain * HEADROOM_GAIN * 32768.0;
		if (y > 32767.0) y = 32767.0;
		else if (y < -32768.0) y = -32768.0;
		return int16_t(std::lround(y));
	}
};

// ---- output resample stage (dtalk_synth16_out) -----------------------------
// Android (and other hosts) hand the raw SAMPLE_RATE (10504Hz) stream to
// their own audio pipeline, which then has to upsample it to the device's
// native mixer rate itself. 10504Hz is not a rate host resamplers are tuned
// for (not a clean ratio to typical rates like 44100/48000), and that
// host-side resample was observed to leave audible imaging artifacts even
// with the reconstruction low-pass (dtalk_set_lowpass_hz) turned all the way
// down - i.e. the noise survives regardless of the corner, because it isn't
// coming from the SAMPLE_RATE-domain signal dtalk produces, it's introduced
// downstream of it. This stage does the upsample here instead, with a filter
// tuned to this exact signal, so a host can just play the result at its
// native rate directly.
//
// Implementation: windowed-sinc ("band-limited interpolation") resampling.
// Each output sample is a weighted sum of the 2*RESAMPLE_N nearest raw
// samples using a Kaiser-windowed sinc kernel with a passband to
// RESAMPLE_CUTOFF_HZ (matched to LPF_HZ_MAX, so the reconstruction filter's
// widest setting survives unattenuated) and a stopband above SAMPLE_RATE -
// RESAMPLE_CUTOFF_HZ, where the raw stream's images would otherwise land
// (verified by measurement: >85dB image rejection above ~5.5kHz with these
// parameters). The kernel depends only on the fixed SAMPLE_RATE, never on
// the chosen output rate, so it is precomputed once into a table indexed by
// (integer tap offset, sub-sample phase) and shared by every instance.
constexpr int RESAMPLE_N = 32;                    // kernel half-width, in raw samples
constexpr double RESAMPLE_BETA = 9.0;             // Kaiser window shape
constexpr double RESAMPLE_CUTOFF_HZ = LPF_HZ_MAX; // matches the widest user corner
constexpr int RESAMPLE_TABLE_STEPS = 1024;        // phase resolution per raw-sample interval

double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	for (int k = 1; k < 40; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
		if (term < 1e-16 * sum) break;
	}
	return sum;
}

double kaiser_window(double x, int half_width, double beta)
{
	double r = x / half_width;
	if (r <= -1.0 || r >= 1.0) return 0.0;
	return bessel_i0(beta * std::sqrt(1.0 - r * r)) / bessel_i0(beta);
}

double windowed_sinc(double x, double fc_ratio)
{
	double u = x * fc_ratio;
	double s = (std::fabs(u) < 1e-9) ? 1.0 : std::sin(DT_PI * u) / (DT_PI * u);
	return s * kaiser_window(x, RESAMPLE_N, RESAMPLE_BETA);
}

// table.at(k, p) = h(k - p/RESAMPLE_TABLE_STEPS), k in [-RESAMPLE_N, RESAMPLE_N),
// p in [0, RESAMPLE_TABLE_STEPS).
struct resample_table
{
	std::vector<double> data;
	resample_table()
	{
		data.resize(size_t(2 * RESAMPLE_N) * RESAMPLE_TABLE_STEPS);
		const double fc_ratio = 2.0 * RESAMPLE_CUTOFF_HZ / double(SAMPLE_RATE);
		for (int k = -RESAMPLE_N; k < RESAMPLE_N; k++)
			for (int p = 0; p < RESAMPLE_TABLE_STEPS; p++)
			{
				double frac = double(p) / RESAMPLE_TABLE_STEPS;
				data[size_t(k + RESAMPLE_N) * RESAMPLE_TABLE_STEPS + p] =
					windowed_sinc(double(k) - frac, fc_ratio);
			}
	}
	double at(int k, int p) const
	{
		return data[size_t(k + RESAMPLE_N) * RESAMPLE_TABLE_STEPS + p];
	}
};

const resample_table &kernel_table()
{
	static resample_table t;
	return t;
}

// Streaming windowed-sinc resampler from SAMPLE_RATE up to a configured
// output rate (upsampling only; see dtalk_set_output_rate).
struct resampler
{
	double step = 1.0;        // SAMPLE_RATE / out_rate: advance per output sample
	double pos = 0.0;         // absolute raw-sample position of the next output sample
	s64 base_index = 0;       // absolute raw-sample index of hist.front()
	std::deque<double> hist;  // buffered raw samples not yet fully consumed
	bool flushed = false;     // true once this utterance's trailing pad has been added

	// A rate change alone needs no reset - glitch-free mid-stream exactly
	// like dtalk_set_lowpass_hz, since hist holds raw SAMPLE_RATE-domain
	// samples whose meaning doesn't depend on the target rate.
	void configure(u32 out_rate)
	{
		step = double(SAMPLE_RATE) / double(out_rate);
	}

	// Clears buffered history (mirrors output_stage::reset), so a resume
	// after dtalk_stop()/dtalk_reset() doesn't blend pre-cut samples into
	// the next utterance.
	void reset()
	{
		hist.clear();
		base_index = 0;
		pos = 0.0;
		flushed = false;
	}

	double sample_at(s64 idx) const
	{
		if (idx < base_index) return 0.0; // before stream start: silence pad
		size_t rel = size_t(idx - base_index);
		return rel < hist.size() ? hist[rel] : 0.0;
	}

	// Raw samples beyond (and including) the current output position that
	// are already buffered - lets the caller size its next raw fetch to the
	// actual deficit instead of a fixed margin, so history can't grow
	// without bound over a long utterance.
	size_t available_ahead() const
	{
		s64 next_index = base_index + s64(hist.size());
		s64 ahead = next_index - s64(std::floor(pos));
		return ahead > 0 ? size_t(ahead) : 0;
	}

	size_t process(const int16_t *in, size_t n_in, int16_t *out, size_t max_out)
	{
		for (size_t i = 0; i < n_in; i++)
			hist.push_back(double(in[i]) / 32768.0);
		const s64 next_index = base_index + s64(hist.size());

		size_t produced = 0;
		const auto &table = kernel_table();
		while (produced < max_out)
		{
			s64 center = s64(std::floor(pos));
			if (center + RESAMPLE_N > next_index) break; // need more raw input
			double frac = pos - double(center);
			int p = int(std::lround(frac * RESAMPLE_TABLE_STEPS));
			if (p >= RESAMPLE_TABLE_STEPS) p = RESAMPLE_TABLE_STEPS - 1;
			if (p < 0) p = 0;
			double acc = 0.0, wsum = 0.0;
			for (int k = -RESAMPLE_N; k < RESAMPLE_N; k++)
			{
				double h = table.at(k, p);
				acc += h * sample_at(center + k);
				wsum += h;
			}
			double y = (wsum != 0.0) ? acc / wsum : 0.0;
			if (y > 1.0) y = 1.0; else if (y < -1.0) y = -1.0;
			out[produced++] = int16_t(std::lround(y * 32767.0));
			pos += step;
		}

		const s64 min_needed = s64(std::floor(pos)) - RESAMPLE_N;
		while (!hist.empty() && base_index < min_needed)
		{
			hist.pop_front();
			base_index++;
		}
		return produced;
	}

	// Pads with silence exactly once per utterance so any tail still
	// buffered (needing right-context beyond the real signal's end) can
	// still be produced; called whenever the raw stream reports genuine
	// idle. Must NOT re-pad on every call - the raw stream stays at "idle"
	// (returning 0) indefinitely once speech ends, and re-adding samples
	// each time would manufacture an endless tail of trailing silence,
	// so dtalk_synth16_out would never see a genuine 0 and never stop.
	size_t flush(int16_t *out, size_t max_out)
	{
		if (flushed)
			return process(nullptr, 0, out, max_out);
		flushed = true;
		int16_t zeros[RESAMPLE_N];
		std::fill(std::begin(zeros), std::end(zeros), int16_t(0));
		return process(zeros, RESAMPLE_N, out, max_out);
	}
};

} // namespace

struct dtalk
{
	doubletalk_board board;
	output_stage out16;                // modeled output stage for dtalk_synth16
	resampler resamp;                  // optional upsample stage for dtalk_synth16_out
	u32 out_rate = 0;                  // 0 = disabled (dtalk_synth16_out == dtalk_synth16)
	std::vector<int16_t> resample_scratch; // raw-rate scratch buffer for dtalk_synth16_out
	std::deque<u8> queue;              // host-side bytes not yet accepted by the card
	std::vector<u8> carry;             // pulled but not yet delivered samples
	size_t carry_off = 0;
	std::deque<dtalk_index_mark> marks;
	u64 samples_base = 0;              // board-grid samples discarded at boot
	u64 samples_dropped = 0;           // grid samples pulled/carried but never delivered (dtalk_stop)
	s64 idle_stable = 0;               // consecutive idle cycles observed
	u32 pause_cap_samples = 0;         // max silence run, in samples; 0 = disabled (authentic)
	u32 silence_run = 0;               // consecutive near-idle samples seen since the last non-silent one
	bool heard_speech = false;         // true once this utterance has produced a non-silent sample
	int rate_boost = 0;                // current boost level (0 = authentic)
	u16 rate_orig[RATE_REGION_HI - RATE_REGION_LO + 1]; // pristine period words
	bool rate_saved = false;

	// Snapshot the stock ROM period table so boost levels can be re-derived
	// from the authentic values rather than compounding.
	void save_rate_table()
	{
		for (int i = RATE_REGION_LO; i <= RATE_REGION_HI; i++)
		{
			u32 off = RATE_TABLE_OFF + 2u * u32(i);
			rate_orig[i - RATE_REGION_LO] =
				u16(board.rom_peek(off)) | u16(board.rom_peek(off + 1)) << 8;
		}
		rate_saved = true;
	}

	// Rescale the in-RAM ROM period table's rate region for the given boost
	// level (always relative to the saved stock values).
	//
	// Final-consonant truncation fix: the firmware's frame engine has a hard
	// floor at period 64 - the frame-render block at CS:0x4cd7 is skipped when
	// the running period <= 0x40 (cmpw $0x40,0x2bc; jle at CS:0x4cd0), which
	// drops that frame's audio. Word-final and phrase-final falling intonation
	// drives the table index up into the region's fast tail (idx 19-22, stock
	// periods 81..73); scaling those by 80% yields 64/61/60/58, i.e. at or
	// below the floor, so the firmware silently drops the final frames and the
	// closing consonant (e.g. the /n/ of "configuration") is cut off. The stock
	// table itself never goes below 73, so we clamp every scaled period to the
	// stock region's own minimum: the firmware then only ever sees periods it
	// already handles natively, no frame falls into the degenerate path, and
	// the speed gain on all the higher-period (steady-state) frames is kept.
	void apply_rate_boost(int level)
	{
		if (!rate_saved)
			save_rate_table();
		if (level < 0) level = 0;
		if (level > RATE_BOOST_MAX) level = RATE_BOOST_MAX;
		const int pct = RATE_BOOST_PCT[level];
		u32 stock_min = 0xffff;
		for (int i = RATE_REGION_LO; i <= RATE_REGION_HI; i++)
			if (rate_orig[i - RATE_REGION_LO] < stock_min)
				stock_min = rate_orig[i - RATE_REGION_LO];
		for (int i = RATE_REGION_LO; i <= RATE_REGION_HI; i++)
		{
			u32 w = u32(rate_orig[i - RATE_REGION_LO]) * u32(pct) / 100u;
			if (w < stock_min) w = stock_min; // never below the firmware's floor
			if (w > 0xffff) w = 0xffff;
			u32 off = RATE_TABLE_OFF + 2u * u32(i);
			board.rom_poke(off, u8(w & 0xff));
			board.rom_poke(off + 1, u8((w >> 8) & 0xff));
		}
		rate_boost = level;
	}

	bool card_busy() const
	{
		return (board.host_status() & 0x40) != 0 // SYNC: speech in progress
			|| board.ram16(BUF_READ_PTR) != board.ram16(BUF_WRITE_PTR);
	}

	void drop_pending_audio()
	{
		// These grid samples advanced the board's absolute output counter but
		// are thrown away instead of delivered by dtalk_synth. Track them so
		// index_events (timestamped in absolute grid samples) can be converted
		// back into delivered-stream positions - otherwise every mark after a
		// cancel would fire late by the running total of discarded audio.
		std::vector<u8> scrap;
		board.pull_samples(scrap, SAMPLE_RATE);
		samples_dropped += scrap.size();
		samples_dropped += carry.size() - carry_off; // pulled-but-undelivered remainder
		board.index_events().clear();
		carry.clear();
		carry_off = 0;
	}

	// Move bytes host->card while the card is RDY; stops (without spinning)
	// as soon as it is not.
	void feed()
	{
		while (!queue.empty() && board.rdy())
		{
			board.host_write(queue.front());
			queue.pop_front();
			// RDY drops 2-3us after a write and returns 180-190us later
			// (dtlk.h); run just past that so the next loop iteration can
			// feed the next byte instead of stalling to the next chunk
			board.run_cycles(2000);
		}
	}

	void pull()
	{
		// Always run silence through the trimmer, even with pause_cap_samples
		// == 0 (authentic mid-utterance pauses): the lead-in cap below still
		// applies before the first sound regardless, so utterance start-up
		// never gets to be slow - see LEADIN_CAP_SAMPLES. Emulating a bit
		// further to make up dropped samples costs far less wall-clock time
		// (the CPU emulation runs well faster than real time) than the
		// silence would have cost being played out through the speaker at
		// 1x, so trimming it nets a real reduction in time-to-audible-sound
		// rather than just trading one delay for another.
		std::vector<u8> raw;
		board.pull_samples(raw, SAMPLE_RATE);
		for (u8 b : raw)
		{
			bool silent = std::abs(int(b) - int(PAUSE_IDLE_BYTE)) <= PAUSE_SILENCE_TOL;
			if (silent)
			{
				u32 cap = heard_speech ? pause_cap_samples : LEADIN_CAP_SAMPLES;
				if (cap != 0 && silence_run >= cap)
				{
					samples_dropped++;
					continue;
				}
				silence_run++;
			}
			else
			{
				silence_run = 0;
				heard_speech = true;
			}
			carry.push_back(b);
		}
		auto &ev = board.index_events();
		while (!ev.empty())
		{
			u64 pos = u64(ev.front().first) * SAMPLE_RATE / doubletalk_board::CPU_HZ;
			// Absolute grid pos -> delivered-stream pos: subtract the boot
			// discard plus everything dtalk_stop has thrown away since.
			u64 offset = samples_base + samples_dropped;
			marks.push_back({ev.front().second, pos > offset ? pos - offset : 0});
			ev.pop_front();
		}
	}

	bool boot()
	{
		const s64 limit = s64(doubletalk_board::CPU_HZ) * 10;
		while (!board.rdy() && board.now_cycles() < limit)
			board.run_cycles(10000);
		if (!board.rdy())
			return false;
		// swallow power-on audio (the genuine hardware DC click)
		board.run_cycles(doubletalk_board::CPU_HZ / 10);
		std::vector<u8> scrap;
		board.pull_samples(scrap, SAMPLE_RATE);
		board.index_events().clear();
		samples_base = u64(board.now_cycles()) * SAMPLE_RATE / doubletalk_board::CPU_HZ;
		return true;
	}
};

extern "C" {

dtalk *dtalk_create(const void *rom, size_t rom_size)
{
	dtalk *dt = new dtalk;
	if (!dt->board.load_rom(static_cast<const u8 *>(rom), rom_size))
	{
		delete dt;
		return nullptr;
	}
	dt->board.reset();
	dt->save_rate_table(); // pristine period table, before any boost
	if (!dt->boot())
	{
		delete dt;
		return nullptr;
	}
	return dt;
}

void dtalk_destroy(dtalk *dt)
{
	delete dt;
}

void dtalk_reset(dtalk *dt)
{
	dt->queue.clear();
	dt->marks.clear();
	dt->carry.clear();
	dt->carry_off = 0;
	dt->samples_dropped = 0;
	dt->idle_stable = 0;
	dt->silence_run = 0;
	dt->heard_speech = false;
	dt->out16.reset();
	dt->resamp.reset();
	dt->board.reset();
	dt->boot();
}

uint32_t dtalk_sample_rate(const dtalk *)
{
	return SAMPLE_RATE;
}

int dtalk_rate_boost_max(void)
{
	return RATE_BOOST_MAX;
}

void dtalk_set_rate_boost(dtalk *dt, int level)
{
	dt->apply_rate_boost(level);
}

int dtalk_get_rate_boost(const dtalk *dt)
{
	return dt->rate_boost;
}

uint8_t dtalk_lpc_status(dtalk *dt)
{
	return dt->board.host_lpc_status();
}

void dtalk_queue(dtalk *dt, const void *bytes, size_t len)
{
	const u8 *p = static_cast<const u8 *>(bytes);
	dt->queue.insert(dt->queue.end(), p, p + len);
	dt->idle_stable = 0;
}

void dtalk_say(dtalk *dt, const char *text)
{
	dtalk_queue(dt, text, std::strlen(text));
	const u8 cr = 0x0d;
	dtalk_queue(dt, &cr, 1);
}

void dtalk_stop(dtalk *dt)
{
	dt->queue.clear();
	dt->marks.clear();
	// Ctrl-X written directly, handshake deliberately ignored (manual:
	// "the states of DoubleTalk's handshaking signals should be ignored
	// when writing the Clear command")
	dt->board.host_write(0x18);
	dt->board.run_cycles(RUN_CHUNK_CYCLES); // let the firmware react
	dt->drop_pending_audio();
	// Clear the output-stage filter history so a resume after this cancel does
	// not replay a discontinuity: prime=true makes the next real sample seed
	// the DC-block at that level, so no boundary step (and thus no re-click) is
	// synthesized across the cut.
	dt->out16.reset();
	dt->resamp.reset();
	dt->idle_stable = 0;
	dt->silence_run = 0;
	dt->heard_speech = false;
}

int dtalk_active(dtalk *dt)
{
	return (!dt->queue.empty() || dt->card_busy()) ? 1 : 0;
}

size_t dtalk_synth(dtalk *dt, uint8_t *out, size_t max_samples)
{
	size_t produced = 0;

	auto drain_carry = [&]()
	{
		size_t avail = dt->carry.size() - dt->carry_off;
		size_t take = std::min(avail, max_samples - produced);
		std::memcpy(out + produced, dt->carry.data() + dt->carry_off, take);
		produced += take;
		dt->carry_off += take;
		if (dt->carry_off == dt->carry.size())
		{
			dt->carry.clear();
			dt->carry_off = 0;
		}
	};

	drain_carry();

	while (produced < max_samples)
	{
		dt->feed();

		if (dt->queue.empty() && !dt->card_busy())
		{
			if (dt->idle_stable >= IDLE_SETTLE_CYCLES)
				break;
			dt->idle_stable += RUN_CHUNK_CYCLES;
		}
		else
			dt->idle_stable = 0;

		dt->board.run_cycles(RUN_CHUNK_CYCLES);
		dt->pull();
		drain_carry();
	}

	return produced;
}

size_t dtalk_synth16(dtalk *dt, int16_t *out, size_t max_samples)
{
	// Same sample-production loop as dtalk_synth, but each raw u8 grid sample
	// is run through the modeled output stage into a signed 16-bit sample.
	size_t produced = 0;

	auto drain_carry = [&]()
	{
		while (produced < max_samples && dt->carry_off < dt->carry.size())
			out[produced++] = dt->out16.process(
				(double(dt->carry[dt->carry_off++]) - 128.0) / 128.0);
		if (dt->carry_off == dt->carry.size())
		{
			dt->carry.clear();
			dt->carry_off = 0;
		}
	};

	drain_carry();

	while (produced < max_samples)
	{
		dt->feed();

		if (dt->queue.empty() && !dt->card_busy())
		{
			if (dt->idle_stable >= IDLE_SETTLE_CYCLES)
				break;
			dt->idle_stable += RUN_CHUNK_CYCLES;
		}
		else
			dt->idle_stable = 0;

		dt->board.run_cycles(RUN_CHUNK_CYCLES);
		dt->pull();
		drain_carry();
	}

	return produced;
}

void dtalk_set_lowpass_hz(dtalk *dt, uint32_t hz)
{
	// 0 restores the datasheet-authentic default; otherwise set_lowpass clamps
	// to LPF_HZ_MIN..LPF_HZ_MAX. Only the biquad coefficients change; the
	// filter's sample history is preserved so a mid-stream change is seamless.
	dt->out16.set_lowpass(hz == 0 ? LPF_HZ : double(hz));
}

void dtalk_set_pause_cap_ms(dtalk *dt, uint32_t ms)
{
	// 0 disables trimming (the authentic firmware pause). Otherwise any
	// silence run longer than ms is cut down to ms - see pull()'s trimming
	// and the pause-shortening note above LPF_HZ_MIN/MAX.
	dt->pause_cap_samples = ms == 0 ? 0 : u32(u64(ms) * SAMPLE_RATE / 1000);
}

void dtalk_set_output_rate(dtalk *dt, uint32_t hz)
{
	if (hz != 0 && hz < SAMPLE_RATE) hz = SAMPLE_RATE; // this stage only upsamples
	if (hz > 192000) hz = 192000;
	dt->out_rate = hz;
	dt->resamp.configure(hz == 0 ? SAMPLE_RATE : hz);
}

uint32_t dtalk_get_output_rate(const dtalk *dt)
{
	return dt->out_rate;
}

size_t dtalk_synth16_out(dtalk *dt, int16_t *out, size_t max_samples)
{
	if (dt->out_rate == 0 || dt->out_rate == SAMPLE_RATE)
		return dtalk_synth16(dt, out, max_samples);

	size_t produced = 0;
	int guard = 0; // defensive iteration cap; the sizing below should always
	                // converge in a handful of iterations
	while (produced < max_samples && guard++ < 64)
	{
		size_t want = max_samples - produced;
		size_t needed = size_t(std::ceil(double(want) * dt->resamp.step)) + 2 * RESAMPLE_N;
		size_t have = dt->resamp.available_ahead();
		size_t raw_want = needed > have ? needed - have : 1;

		if (dt->resample_scratch.size() < raw_want)
			dt->resample_scratch.resize(raw_want);
		size_t raw_n = dtalk_synth16(dt, dt->resample_scratch.data(), raw_want);
		if (raw_n > 0)
			produced += dt->resamp.process(dt->resample_scratch.data(), raw_n, out + produced, want);
		if (raw_n == 0)
		{
			produced += dt->resamp.flush(out + produced, max_samples - produced);
			break;
		}
	}
	return produced;
}

size_t dtalk_read_index_marks(dtalk *dt, dtalk_index_mark *out, size_t max)
{
	size_t n = 0;
	while (n < max && !dt->marks.empty())
	{
		out[n++] = dt->marks.front();
		dt->marks.pop_front();
	}
	return n;
}

} // extern "C"
