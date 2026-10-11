#pragma once

#include "Emu/Audio/AudioBackend.h"
#include "Utilities/Thread.h"

#include <vector>

// Output through SceAudioOut: a thread pulls a grain from cellAudio's write callback and
// hands it to the port; sceAudioOutOutput blocks until the port takes it, which paces it.
class ps5_audio_backend final : public AudioBackend
{
public:
	ps5_audio_backend();
	~ps5_audio_backend() override;

	std::string_view GetName() const override { return "SceAudioOut"sv; }

	bool Initialized() override { return m_initialized; }
	bool Operational() override { return m_port >= 0; }

	bool Open(std::string_view dev_id, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout) override;
	void Close() override;

	f64 GetCallbackFrameLen() override { return static_cast<f64>(grain) / 48000.0; }

	void Play() override;
	void Pause() override;

private:
	static constexpr u32 grain = 256; // samples per sceAudioOutOutput

	bool m_initialized = false;
	s32 m_port = -1;
	std::vector<u8> m_buffer;
	std::unique_ptr<named_thread<std::function<void()>>> m_thread;
	atomic_t<bool> m_stop{false};

	// Level meter over the grains handed to the port. Silent while audio is flowing; one error
	// line per 1024 grains (~5.5 s) of unbroken digital silence, and one notice when it ends.
	u32 m_meter_grains = 0;
	u32 m_meter_silent_grains = 0;
	f32 m_meter_peak = 0.0f;
	bool m_meter_reported_silence = false;

	void output_loop();
	void MeterGrain(u32 bytes);
};
