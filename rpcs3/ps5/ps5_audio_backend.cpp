#include "stdafx.h"
#include "ps5_audio_backend.h"
#include "ps5_sce.h"

LOG_CHANNEL(ps5_audio_log, "SceAudioOut");

ps5_audio_backend::ps5_audio_backend()
{
	// sceAudioOutInit may be called more than once; anything but a failure is fine
	if (const int res = sceAudioOutInit(); res < 0 && res != static_cast<int>(0x8026000e))
	{
		ps5_audio_log.error("sceAudioOutInit() failed: 0x%x", res);
		return;
	}

	m_initialized = true;
}

ps5_audio_backend::~ps5_audio_backend()
{
	Close();
}

bool ps5_audio_backend::Open(std::string_view /*dev_id*/, AudioFreq freq, AudioSampleSize /*sample_size*/, AudioChannelCnt ch_cnt, audio_channel_layout layout)
{
	if (!m_initialized)
		return false;

	Close();
	std::lock_guard lock{m_cb_mutex};

	if (freq != AudioFreq::FREQ_48K)
	{
		ps5_audio_log.error("Only 48 kHz output is supported (requested %u Hz)", static_cast<u32>(freq));
		return false;
	}

	m_sampling_rate = freq;

	// Always s16, whatever Audio: Convert to 16 bit says. The console accepts a float stereo
	// port - sceAudioOutOpen returns a valid handle and nothing about the log looks wrong - and
	// then plays nothing through it (measured 2026-10-10, Jak and Daxter Collection). The Xbox
	// port on this console hardcodes the same choice as SceAudioOut parameter 1.
	//
	// Forcing it here rather than in the config keeps both sides in step: the mixer asks this
	// backend which format to hand over (AudioBackend::get_convert_to_s16() is only
	// `m_sample_size == S16`, read by Emu/Cell/Modules/cellAudio.cpp:308), so overriding the
	// sample size fixes the conversion and the port together. Do not set one without the other.
	m_sample_size = AudioSampleSize::S16;

	// The port is stereo; RPCS3 downmixes 5.1/7.1 to it
	setup_channel_layout(static_cast<u32>(ch_cnt), 2, layout, ps5_audio_log);

	const bool s16 = get_convert_to_s16();
	const u32 format = s16 ? ps5::AUDIO_OUT_S16_STEREO : ps5::AUDIO_OUT_FLOAT_STEREO;

	m_port = sceAudioOutOpen(ps5::AUDIO_OUT_USER_SYSTEM, 0, 0, grain, 48000, format);

	if (m_port < 0)
	{
		ps5_audio_log.error("sceAudioOutOpen() failed: 0x%x", m_port);
		m_port = -1;
		return false;
	}

	m_buffer.assign(grain * get_channels() * get_sample_size(), 0);
	ps5_audio_log.notice("Opened port %d: %s stereo, grain %u", m_port, s16 ? "s16" : "float", grain);

	m_stop = false;
	m_thread = std::make_unique<named_thread<std::function<void()>>>("SceAudioOut", [this]() { output_loop(); });
	return true;
}

void ps5_audio_backend::Close()
{
	if (m_thread)
	{
		m_stop = true;
		m_thread.reset(); // joins
	}

	std::lock_guard lock{m_cb_mutex};

	if (m_port >= 0)
	{
		sceAudioOutClose(m_port);
		m_port = -1;
	}

	m_playing = false;
}

void ps5_audio_backend::Play()
{
	std::lock_guard lock{m_cb_mutex};
	m_playing = true;
}

void ps5_audio_backend::Pause()
{
	std::lock_guard lock{m_cb_mutex};
	m_playing = false;
}

void ps5_audio_backend::output_loop()
{
	while (!m_stop && thread_ctrl::state() != thread_state::aborting)
	{
		u32 written = 0;
		const u32 bytes = ::size32(m_buffer);

		{
			std::unique_lock lock(m_cb_mutex, std::defer_lock);

			if (lock.try_lock_for(std::chrono::microseconds{50}) && m_write_callback && m_playing)
			{
				written = std::min(m_write_callback(bytes, m_buffer.data()), bytes);
			}
		}

		// Silence for what the callback did not fill
		std::memset(m_buffer.data() + written, 0, bytes - written);

		MeterGrain(bytes);

		// Blocks until the port takes the grain
		sceAudioOutOutput(m_port, m_buffer.data());
	}

	// Flush: an output of null waits for the port to drain
	sceAudioOutOutput(m_port, nullptr);
}

// Measure what is about to be handed to the console. Adopted from the Xbox-on-PS5 port,
// which meters each frame for exactly this reason (PS5X360, platform/ps5/native_audio.cpp
// MeterFrame): a silent port can then be told apart from a silent *mix* - if the peak here
// is zero, the console was handed silence and the fault is upstream of SceAudioOut.
void ps5_audio_backend::MeterGrain(u32 bytes)
{
	f32 peak = 0.0f;

	if (get_convert_to_s16())
	{
		const s16* samples = reinterpret_cast<const s16*>(m_buffer.data());

		for (u32 i = 0, count = bytes / sizeof(s16); i < count; i++)
		{
			const f32 sample = samples[i] / 32768.0f;
			peak = std::max(peak, sample < 0.0f ? -sample : sample);
		}
	}
	else
	{
		const f32* samples = reinterpret_cast<const f32*>(m_buffer.data());

		for (u32 i = 0, count = bytes / sizeof(f32); i < count; i++)
		{
			const f32 sample = samples[i];
			peak = std::max(peak, sample < 0.0f ? -sample : sample);
		}
	}

	m_meter_peak = std::max(m_meter_peak, peak);

	if (peak == 0.0f)
	{
		m_meter_silent_grains++;
	}

	if (++m_meter_grains >= 1024)
	{
		// Speak up only when something is wrong. A healthy stream logs nothing: a line every
		// 5.5 s for the whole session would bury whatever else the log is being read for. One
		// window of unbroken digital silence while playing is the symptom worth reporting, and
		// the recovery line says when it ends.
		const bool all_silent = m_meter_silent_grains == m_meter_grains;

		if (all_silent && m_playing)
		{
			ps5_audio_log.error("Audio level: digital silence for %u grains (%u ms) while playing, port=%d. "
				"The console is being handed silence, so the fault is upstream of SceAudioOut.",
				m_meter_grains, m_meter_grains * 1000 * 256 / 48000, m_port);
		}
		else if (!all_silent && m_meter_reported_silence)
		{
			ps5_audio_log.notice("Audio level: recovered, peak %.4f of 1.0, port=%d", m_meter_peak, m_port);
		}

		m_meter_reported_silence = all_silent;
		m_meter_peak = 0.0f;
		m_meter_silent_grains = 0;
		m_meter_grains = 0;
	}
}
