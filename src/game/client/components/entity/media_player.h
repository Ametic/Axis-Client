// https://github.com/wxj881027/QmClient
#ifndef GAME_CLIENT_COMPONENTS_ENTITY_MEDIA_PLAYER_H
#define GAME_CLIENT_COMPONENTS_ENTITY_MEDIA_PLAYER_H

#include <engine/graphics.h>

#include <game/client/component.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

class CMediaViewer : public CComponent
{
public:
	class CVisualizer
	{
	public:
		static constexpr int NUM_FREQUENCY_BANDS = 64; // Number of frequency bars
		std::array<float, NUM_FREQUENCY_BANDS> m_aFrequencyBands; // 0.0 to 1.0
		bool m_Active = false;

		void GetBands(float *pOutBands, int NumBands) const;
		float GetAverageBand() const;
	};

	class CState
	{
	public:
		bool m_CanPlay = false;
		bool m_CanPause = false;
		bool m_CanPrev = false;
		bool m_CanNext = false;
		bool m_Playing = false;
		std::string m_ServiceId;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		IGraphics::CTextureHandle m_AlbumArt;
		int m_AlbumArtWidth = 0;
		int m_AlbumArtHeight = 0;
		CVisualizer m_Visualizer;
	};

#if defined(CONF_FAMILY_WINDOWS) && __has_include(<winrt/Windows.Foundation.h>)
	struct SWinrt;
	struct SShared;
	struct SAudioCapture;
#endif

	CMediaViewer();
	~CMediaViewer() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;

	bool GetStateSnapshot(CState &State) const;
	void Previous();
	void PlayPause();
	void Next();

private:
#if defined(CONF_FAMILY_WINDOWS) && __has_include(<winrt/Windows.Foundation.h>)
	std::unique_ptr<SWinrt> m_pWinrt;
	std::unique_ptr<SShared> m_pShared;
	std::unique_ptr<SAudioCapture> m_pAudioCapture;
	std::thread m_Thread;
	std::thread m_AudioThread;
	std::atomic_bool m_StopThread = false;
	std::atomic_bool m_StopAudioThread = false;

	void ThreadMain();
	void AudioThreadMain();
	void ProcessAudioFrame(const float *pSamples, int NumSamples, int SampleRate);
#endif
};

#endif
