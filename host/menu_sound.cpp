// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sounds of PS2Native's menu over a game (menu_sound.h; docs/adr/0060).
//
// In the manner of the PlayStation 2's own menus, at the maintainer's word
// ("Use PS2 style sounds"): glassy, crystalline tones in a large, soft, dark
// room, a breath of air and a low swell when the menu opens. They are made here,
// note by note: nothing of Sony's own sounds, which are the console's BIOS's and
// never taken from it.

#include "menu_sound.h"

#include "common/FileSystem.h"
#include "common/Path.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Config.h"

#include "fmt/format.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numbers>
#include <vector>

namespace ps2n::host::menu::sound
{
	namespace
	{
		constexpr u32 kRate = 48000;
		constexpr float kPi = std::numbers::pi_v<float>;
		constexpr float kTau = 2.0f * kPi;

		// Two channels, made one sound at a time and put together at the end.
		class Synth
		{
		public:
			// A glassy bell: a sine whose phase another, `ratio` times as fast,
			// moves (frequency modulation), the movement dying away faster than the
			// tone, so that it is struck bright and rings pure. The right side a
			// few cents above the left, so that the two shimmer against each other.
			// Placed from the left (-1) to the right (1).
			void Bell(float start, float frequency, float gain, float decay, float pan, float ratio = 3.0f, float index = 1.2f,
				float shimmer_cents = 4.0f)
			{
				const size_t first = Frames(start);
				const size_t count = Frames(decay * 7.0f + 0.01f);
				Ensure(first + count);
				const float angle = (pan + 1.0f) * kPi * 0.25f;
				const float to_left = std::cos(angle), to_right = std::sin(angle);
				const float detune = std::pow(2.0f, shimmer_cents / 1200.0f);
				const float index_decay = std::max(0.004f, decay * 0.18f);
				for (size_t i = 0; i < count; i++)
				{
					const float t = static_cast<float>(i) / kRate;
					constexpr float attack = 0.0025f;
					const float envelope = (t < attack) ? t / attack : std::exp(-(t - attack) / decay);
					const float modulation = index * std::exp(-t / index_decay);
					const float left = std::sin(kTau * frequency * t + modulation * std::sin(kTau * frequency * ratio * t));
					const float right =
						std::sin(kTau * frequency * detune * t + modulation * std::sin(kTau * frequency * detune * ratio * t));
					m_left[first + i] += gain * envelope * left * to_left;
					m_right[first + i] += gain * envelope * right * to_right;
				}
			}

			// A soft held tone under the bells: a sine with a little of its octave,
			// swelling in over `attack`, held, and fading over `release`, with a
			// slow, slight vibrato.
			void Pad(float start, float frequency, float gain, float attack, float hold, float release, float pan)
			{
				const size_t first = Frames(start);
				const size_t count = Frames(attack + hold + release);
				Ensure(first + count);
				const float angle = (pan + 1.0f) * kPi * 0.25f;
				const float to_left = std::cos(angle), to_right = std::sin(angle);
				float phase = 0.0f;
				for (size_t i = 0; i < count; i++)
				{
					const float t = static_cast<float>(i) / kRate;
					float envelope;
					if (t < attack)
						envelope = 0.5f - 0.5f * std::cos(kPi * t / attack);
					else if (t < attack + hold)
						envelope = 1.0f;
					else
						envelope = 0.5f + 0.5f * std::cos(kPi * std::min(1.0f, (t - attack - hold) / release));
					phase += kTau * frequency * (1.0f + 0.0017f * std::sin(kTau * 5.0f * t)) / kRate;
					const float value = std::sin(phase) + 0.12f * std::sin(2.0f * phase);
					m_left[first + i] += gain * envelope * value * to_left;
					m_right[first + i] += gain * envelope * value * to_right;
				}
			}

			// A low swell: a sine gliding from `from` to `to` hertz, rising and
			// falling away.
			void Swell(float start, float length, float from, float to, float gain)
			{
				const size_t first = Frames(start);
				const size_t count = Frames(length);
				Ensure(first + count);
				float phase = 0.0f;
				for (size_t i = 0; i < count; i++)
				{
					const float x = static_cast<float>(i) / static_cast<float>(count);
					const float frequency = from * std::pow(to / from, x);
					phase += kTau * frequency / kRate;
					const float envelope = std::sin(kPi * std::pow(x, 0.6f));
					const float value = gain * envelope * envelope * std::sin(phase);
					m_left[first + i] += value;
					m_right[first + i] += value;
				}
			}

			// A breath of air: noise in a band whose top glides from `from` to `to`
			// hertz (two low-pass filters, one subtracted from the other), swelling
			// and fading, different on each side.
			void Air(float start, float length, float from, float to, float gain)
			{
				const size_t first = Frames(start);
				const size_t count = Frames(length);
				Ensure(first + count);
				u32 seeds[2] = {0x2545F491u, 0x9E3779B9u};
				float high[2] = {}, low[2] = {};
				const auto noise = [](u32& seed) {
					seed = seed * 1664525u + 1013904223u;
					return static_cast<float>(seed >> 8) / static_cast<float>(1u << 23) - 1.0f;
				};
				for (size_t i = 0; i < count; i++)
				{
					const float x = static_cast<float>(i) / static_cast<float>(count);
					const float cutoff = from * std::pow(to / from, x);
					const float a_high = 1.0f - std::exp(-kTau * cutoff / kRate);
					const float a_low = 1.0f - std::exp(-kTau * cutoff * 0.2f / kRate);
					const float swell = std::sin(kPi * x);
					for (int side = 0; side < 2; side++)
					{
						const float n = noise(seeds[side]);
						high[side] += a_high * (n - high[side]);
						low[side] += a_low * (n - low[side]);
						(side == 0 ? m_left : m_right)[first + i] += gain * swell * swell * (high[side] - low[side]);
					}
				}
			}

			// A room around it all: Jezar's Freeverb (eight parallel combs with
			// damping, four allpasses), its delays a little longer on the right
			// than on the left, after a short silence (`predelay`). `size` and
			// `damping` from 0 to 1 as Freeverb's; `tail` seconds more of it.
			void Room(float wet, float size, float damping, float predelay, float tail)
			{
				Ensure(m_left.size() + Frames(tail));
				Reverberate(m_left, wet, size, damping, Frames(predelay), 0);
				Reverberate(m_right, wet, size, damping, Frames(predelay), 23);
			}

			// Interleaved, its loudest at `peak`, ended where it has died away to
			// 56 dB under that, its last 20 ms faded out.
			std::vector<float> Finish(float peak) const
			{
				float loudest = 0.0f;
				for (size_t i = 0; i < m_left.size(); i++)
					loudest = std::max({loudest, std::abs(m_left[i]), std::abs(m_right[i])});
				const float scale = (loudest > 0.0f) ? peak / loudest : 0.0f;
				size_t length = m_left.size();
				while (length > 0 && std::max(std::abs(m_left[length - 1]), std::abs(m_right[length - 1])) < loudest * 0.0016f)
					length--;
				const size_t fade = std::min(length, Frames(0.020f));
				std::vector<float> out(length * 2);
				for (size_t i = 0; i < length; i++)
				{
					const size_t to_end = length - i;
					const float end = (to_end < fade) ? static_cast<float>(to_end) / static_cast<float>(fade) : 1.0f;
					out[i * 2] = m_left[i] * scale * end;
					out[i * 2 + 1] = m_right[i] * scale * end;
				}
				return out;
			}

		private:
			static size_t Frames(float seconds)
			{
				return static_cast<size_t>(std::max(0.0f, seconds) * kRate);
			}

			void Ensure(size_t frames)
			{
				if (m_left.size() < frames)
				{
					m_left.resize(frames, 0.0f);
					m_right.resize(frames, 0.0f);
				}
			}

			static void Reverberate(std::vector<float>& channel, float wet, float size, float damping, size_t predelay, size_t spread)
			{
				struct Comb
				{
					std::vector<float> line;
					size_t at = 0;
					float stored = 0.0f;
				};
				struct Allpass
				{
					std::vector<float> line;
					size_t at = 0;
				};
				// Freeverb's delays at 44.1 kHz, scaled to this rate.
				static constexpr size_t kCombs[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
				static constexpr size_t kAllpasses[] = {556, 441, 341, 225};
				const float ratio = static_cast<float>(kRate) / 44100.0f;
				std::array<Comb, std::size(kCombs)> combs;
				std::array<Allpass, std::size(kAllpasses)> allpasses;
				for (size_t i = 0; i < combs.size(); i++)
					combs[i].line.assign(static_cast<size_t>((kCombs[i] + spread) * ratio), 0.0f);
				for (size_t i = 0; i < allpasses.size(); i++)
					allpasses[i].line.assign(static_cast<size_t>((kAllpasses[i] + spread) * ratio), 0.0f);

				// Freeverb's scaling of its two settings.
				const float feedback = 0.7f + 0.28f * std::clamp(size, 0.0f, 1.0f);
				const float damp = 0.4f * std::clamp(damping, 0.0f, 1.0f);
				std::vector<float> late(channel.size(), 0.0f);
				for (size_t n = 0; n < channel.size(); n++)
				{
					const float in = (n >= predelay ? channel[n - predelay] : 0.0f) * 0.015f;
					float out = 0.0f;
					for (Comb& comb : combs)
					{
						const float delayed = comb.line[comb.at];
						comb.stored = delayed * (1.0f - damp) + comb.stored * damp;
						comb.line[comb.at] = in + comb.stored * feedback;
						comb.at = (comb.at + 1) % comb.line.size();
						out += delayed;
					}
					for (Allpass& pass : allpasses)
					{
						const float delayed = pass.line[pass.at];
						const float through = -out + delayed;
						pass.line[pass.at] = out + delayed * 0.5f;
						pass.at = (pass.at + 1) % pass.line.size();
						out = through;
					}
					late[n] = out;
				}
				for (size_t n = 0; n < channel.size(); n++)
					channel[n] += wet * 3.0f * late[n];
			}

			std::vector<float> m_left, m_right;
		};

		// The notes: an open D major ninth's (D, A, E, F sharp) in high octaves,
		// and B.
		constexpr float kD5 = 587.33f, kA5 = 880.00f, kB5 = 987.77f, kD6 = 1174.66f, kE6 = 1318.51f, kFs6 = 1479.98f,
						kA6 = 1760.00f, kE7 = 2637.02f, kA7 = 3520.00f;

		std::vector<float> Make(Sound sound)
		{
			Synth synth;
			switch (sound)
			{
				case Sound::Open:
					// A low swell and a breath of air rising, glassy notes blooming over
					// a soft fifth, in a large dark room.
					synth.Swell(0.0f, 0.55f, 68.0f, 104.0f, 0.30f);
					synth.Air(0.0f, 0.50f, 220.0f, 3400.0f, 0.22f);
					synth.Pad(0.04f, kD5, 0.10f, 0.16f, 0.22f, 0.50f, -0.25f);
					synth.Pad(0.04f, kA5, 0.08f, 0.18f, 0.20f, 0.50f, 0.25f);
					synth.Bell(0.06f, kD6, 0.40f, 0.34f, -0.45f, 3.0f, 1.3f);
					synth.Bell(0.15f, kA6, 0.34f, 0.32f, 0.05f, 3.0f, 1.2f);
					synth.Bell(0.24f, kE7, 0.28f, 0.30f, 0.45f, 3.0f, 1.0f);
					synth.Bell(0.33f, kFs6, 0.16f, 0.40f, -0.10f, 2.0f, 0.8f);
					synth.Room(0.42f, 0.86f, 0.45f, 0.022f, 1.6f);
					return synth.Finish(0.32f);

				case Sound::Close:
					// The same falling away.
					synth.Air(0.0f, 0.36f, 3000.0f, 200.0f, 0.18f);
					synth.Swell(0.02f, 0.40f, 104.0f, 60.0f, 0.22f);
					synth.Bell(0.00f, kE7, 0.26f, 0.20f, 0.45f, 3.0f, 1.0f);
					synth.Bell(0.07f, kA6, 0.32f, 0.22f, 0.05f, 3.0f, 1.1f);
					synth.Bell(0.14f, kD6, 0.38f, 0.26f, -0.45f, 3.0f, 1.2f);
					synth.Room(0.34f, 0.72f, 0.55f, 0.020f, 1.0f);
					return synth.Finish(0.28f);

				case Sound::Point:
					// A soft, crystalline tick, with a little of the room.
					synth.Bell(0.0f, kE7 * 1.19f, 0.60f, 0.022f, 0.0f, 2.0f, 0.9f, 2.0f);
					synth.Bell(0.0f, kE7 * 0.595f, 0.14f, 0.018f, 0.0f, 2.0f, 0.4f, 0.0f);
					synth.Room(0.10f, 0.40f, 0.60f, 0.010f, 0.25f);
					return synth.Finish(0.09f);

				case Sound::Choose:
					// Two bright glassy notes up, a faint sparkle over them.
					synth.Bell(0.00f, kA6, 0.50f, 0.14f, -0.20f, 3.0f, 1.5f);
					synth.Bell(0.05f, kE7, 0.44f, 0.18f, 0.20f, 3.0f, 1.3f);
					synth.Bell(0.10f, kA7, 0.14f, 0.09f, 0.40f, 2.0f, 0.6f);
					synth.Room(0.26f, 0.58f, 0.55f, 0.015f, 0.6f);
					return synth.Finish(0.25f);

				case Sound::Back:
					// Two duller notes down.
					synth.Bell(0.00f, kE6, 0.46f, 0.10f, 0.15f, 2.0f, 0.6f);
					synth.Bell(0.06f, kB5, 0.50f, 0.13f, -0.15f, 2.0f, 0.5f);
					synth.Room(0.24f, 0.55f, 0.65f, 0.015f, 0.5f);
					return synth.Finish(0.21f);

				default:
					return {};
			}
		}

		constexpr const char* kNames[] = {"open", "close", "point", "choose", "back"};
		static_assert(std::size(kNames) == static_cast<size_t>(Sound::Count));

		// SDL's audio: one device, and for each sound a few streams, which SDL
		// mixes, taken in turn: a sound played again while it still rings does
		// not cut it short (the ticks of a cursor moved quickly).
		constexpr size_t kStreams[] = {1, 1, 4, 2, 2};
		static_assert(std::size(kStreams) == static_cast<size_t>(Sound::Count));
		constexpr size_t kMostStreams = 4;

		struct Player
		{
			std::once_flag started;
			bool ready = false;
			SDL_AudioDeviceID device = 0;
			std::array<std::array<SDL_AudioStream*, kMostStreams>, static_cast<size_t>(Sound::Count)> streams = {};
			std::array<std::atomic<u32>, static_cast<size_t>(Sound::Count)> next = {};
			std::array<std::vector<float>, static_cast<size_t>(Sound::Count)> sounds;
		};
		Player s_player;
		std::array<std::atomic<u32>, static_cast<size_t>(Sound::Count)> s_counts = {};

		void Start()
		{
			for (size_t i = 0; i < s_player.sounds.size(); i++)
				s_player.sounds[i] = Make(static_cast<Sound>(i));

			if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
			{
				std::fprintf(stderr, "ps2n-host: the menu's sounds: no audio (%s)\n", SDL_GetError());
				return;
			}
			const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, static_cast<int>(kRate)};
			s_player.device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
			if (s_player.device == 0)
			{
				std::fprintf(stderr, "ps2n-host: the menu's sounds: no audio device (%s)\n", SDL_GetError());
				SDL_QuitSubSystem(SDL_INIT_AUDIO);
				return;
			}
			for (size_t i = 0; i < s_player.streams.size(); i++)
			{
				for (size_t j = 0; j < kStreams[i]; j++)
				{
					SDL_AudioStream*& stream = s_player.streams[i][j];
					stream = SDL_CreateAudioStream(&spec, &spec);
					if (!stream || !SDL_BindAudioStream(s_player.device, stream))
					{
						std::fprintf(stderr, "ps2n-host: the menu's sounds: %s\n", SDL_GetError());
						return;
					}
				}
			}
			s_player.ready = true;
			std::fprintf(stderr, "ps2n-host: the menu's sounds: on %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "SDL");
		}

		// A WAV file of 16-bit samples.
		bool WriteWav(const std::string& path, const std::vector<float>& samples)
		{
			std::FILE* file = FileSystem::OpenCFile(path.c_str(), "wb");
			if (!file)
				return false;
			const u32 data_bytes = static_cast<u32>(samples.size() * sizeof(s16));
			const auto u32le = [file](u32 v) { std::fwrite(&v, 4, 1, file); };
			const auto u16le = [file](u16 v) { std::fwrite(&v, 2, 1, file); };
			std::fwrite("RIFF", 1, 4, file);
			u32le(36 + data_bytes);
			std::fwrite("WAVEfmt ", 1, 8, file);
			u32le(16);
			u16le(1); // PCM
			u16le(2);
			u32le(kRate);
			u32le(kRate * 4);
			u16le(4);
			u16le(16);
			std::fwrite("data", 1, 4, file);
			u32le(data_bytes);
			for (const float sample : samples)
			{
				const s16 value = static_cast<s16>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
				std::fwrite(&value, sizeof(value), 1, file);
			}
			const bool ok = std::ferror(file) == 0;
			return (std::fclose(file) == 0) && ok;
		}
	} // namespace

	void Play(Sound sound)
	{
		const size_t which = static_cast<size_t>(sound);
		if (which >= s_counts.size())
			return;
		s_counts[which]++;

		// As the game's own sound: nothing when it is muted, at its volume
		// otherwise.
		if (EmuConfig.SPU2.OutputMuted || EmuConfig.SPU2.StandardVolume == 0)
			return;
		std::call_once(s_player.started, Start);
		if (!s_player.ready)
			return;
		SDL_AudioStream* stream = s_player.streams[which][s_player.next[which]++ % kStreams[which]];
		const std::vector<float>& samples = s_player.sounds[which];
		SDL_ClearAudioStream(stream);
		SDL_SetAudioStreamGain(stream, static_cast<float>(EmuConfig.SPU2.StandardVolume) / 100.0f);
		SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
	}

	std::string Counts()
	{
		std::string text;
		for (size_t i = 0; i < s_counts.size(); i++)
			text += fmt::format("{}{} {}", text.empty() ? "" : ", ", kNames[i], s_counts[i].load());
		return text;
	}

	bool WriteFiles(const std::string& folder)
	{
		if (!FileSystem::EnsureDirectoryExists(folder.c_str(), true))
			return false;
		bool ok = true;
		for (size_t i = 0; i < static_cast<size_t>(Sound::Count); i++)
			ok = WriteWav(Path::Combine(folder, fmt::format("{}.wav", kNames[i])), Make(static_cast<Sound>(i))) && ok;
		return ok;
	}

	void Shutdown()
	{
		if (!s_player.ready)
			return;
		for (auto& streams : s_player.streams)
		{
			for (SDL_AudioStream*& stream : streams)
			{
				if (stream)
					SDL_DestroyAudioStream(stream);
				stream = nullptr;
			}
		}
		SDL_CloseAudioDevice(s_player.device);
		s_player.device = 0;
		s_player.ready = false;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
} // namespace ps2n::host::menu::sound
