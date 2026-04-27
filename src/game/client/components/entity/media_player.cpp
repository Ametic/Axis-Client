// https://github.com/wxj881027/QmClient
#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS) && __has_include(<winrt/Windows.Foundation.h>)
#define MEDIA_PLAYER_WINRT 1

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef NOGDI
#undef NOGDI
#endif

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Audio.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.Render.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#ifdef ERROR
#undef ERROR
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>
#else
#define MEDIA_PLAYER_WINRT 0
#endif

#include "media_player.h"

#include <base/system.h>

#include <engine/image.h>
#define IStorage EngineIStorage
#include <engine/shared/config.h>
#undef IStorage

#if MEDIA_PLAYER_WINRT
using namespace winrt::Windows::Media::Control;

struct CMediaViewer::SWinrt
{
	CMediaViewer::CState m_State{};
	bool m_HasMedia = false;
};

struct SPlainState
{
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
};

enum class ECommand
{
	Prev,
	PlayPause,
	Next,
};

class CMediaViewer::CShared
{
public:
	std::mutex m_Mutex;
	SPlainState m_State{};
	bool m_HasMedia = false;
	std::deque<ECommand> m_Commands;
	std::vector<uint8_t> m_AlbumArtRgba;
	int m_AlbumArtWidth = 0;
	int m_AlbumArtHeight = 0;
	bool m_AlbumArtDirty = false;
};

class CMediaViewer::CAudioCapture
{
public:
	std::mutex m_Mutex;
	std::array<float, CMediaViewer::CVisualizer::NUM_FREQUENCY_BANDS> m_aFrequencyBands{};
	bool m_Active = false;

	int64_t m_LastFrequencyChange = 0;
};

namespace FFT
{
	constexpr int FFT_SIZE = 768;

	class CComplex
	{
	public:
		float Real;
		float Imag;

		CComplex(float r = 0.0f, float i = 0.0f) :
			Real(r), Imag(i) {}

		CComplex operator+(const CComplex &other) const
		{
			return CComplex(Real + other.Real, Imag + other.Imag);
		}

		CComplex operator-(const CComplex &other) const
		{
			return CComplex(Real - other.Real, Imag - other.Imag);
		}

		CComplex operator*(const CComplex &other) const
		{
			return CComplex(
				Real * other.Real - Imag * other.Imag,
				Real * other.Imag + Imag * other.Real);
		}

		float Magnitude() const
		{
			return std::sqrt(Real * Real + Imag * Imag);
		}
	};

	void FFTRecursive(std::vector<CComplex> &x)
	{
		const int N = x.size();
		if(N <= 1)
			return;

		// Divide
		std::vector<CComplex> even(N / 2);
		std::vector<CComplex> odd(N / 2);
		for(int i = 0; i < N / 2; ++i)
		{
			even[i] = x[i * 2];
			odd[i] = x[i * 2 + 1];
		}

		// Conquer
		FFTRecursive(even);
		FFTRecursive(odd);

		// Combine
		for(int k = 0; k < N / 2; ++k)
		{
			const float angle = -2.0f * std::numbers::pi_v<float> * k / N;
			const CComplex t = CComplex(std::cos(angle), std::sin(angle)) * odd[k];
			x[k] = even[k] + t;
			x[k + N / 2] = even[k] - t;
		}
	}

	void ComputeFFT(const float *pSamples, int NumSamples, std::vector<CComplex> &Output)
	{
		const int N = std::min(NumSamples, FFT_SIZE);
		Output.resize(FFT_SIZE);

		// Apply Hamming window and copy samples
		for(int i = 0; i < N; ++i)
		{
			const float window = 0.54f - 0.46f * std::cos(2.0f * std::numbers::pi_v<float> * i / (N - 1));
			Output[i] = CComplex(pSamples[i] * window, 0.0f);
		}

		// Zero padding
		for(int i = N; i < FFT_SIZE; ++i)
		{
			Output[i] = CComplex(0.0f, 0.0f);
		}

		FFTRecursive(Output);
	}
}

template<typename TAsyncOp>
static bool WaitForAsync(const TAsyncOp &Operation, const std::atomic_bool &StopFlag)
{
	using winrt::Windows::Foundation::AsyncStatus;
	while(true)
	{
		const AsyncStatus Status = Operation.Status();
		if(Status == AsyncStatus::Completed)
			return true;
		if(Status == AsyncStatus::Canceled || Status == AsyncStatus::Error)
			return false;
		if(StopFlag.load(std::memory_order_relaxed))
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

static void ClearAlbumArtLocal(CMediaViewer::SWinrt *pWinrt, IGraphics *pGraphics)
{
	if(pGraphics && pWinrt->m_State.m_AlbumArt.IsValid())
	{
		pGraphics->UnloadTexture(&pWinrt->m_State.m_AlbumArt);
	}
	pWinrt->m_State.m_AlbumArt.Invalidate();
	pWinrt->m_State.m_AlbumArtWidth = 0;
	pWinrt->m_State.m_AlbumArtHeight = 0;
}

static void ClearState(CMediaViewer::SWinrt *pWinrt, IGraphics *pGraphics)
{
	ClearAlbumArtLocal(pWinrt, pGraphics);
	pWinrt->m_State = CMediaViewer::CState{};
	pWinrt->m_HasMedia = false;
}

static void ClearSharedAlbumArt(CMediaViewer::CShared *pShared)
{
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_AlbumArtRgba.clear();
	pShared->m_AlbumArtWidth = 0;
	pShared->m_AlbumArtHeight = 0;
	pShared->m_AlbumArtDirty = true;
}

static void SetSharedAlbumArt(CMediaViewer::CShared *pShared, std::vector<uint8_t> &&Pixels, int Width, int Height)
{
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_AlbumArtRgba = std::move(Pixels);
	pShared->m_AlbumArtWidth = Width;
	pShared->m_AlbumArtHeight = Height;
	pShared->m_AlbumArtDirty = true;
}

static void ClearMediaText(SPlainState &State)
{
	State.m_ServiceId.clear();
	State.m_Title.clear();
	State.m_Artist.clear();
	State.m_Album.clear();
}

static void ClearMediaDetails(SPlainState &State, std::string &AlbumArtKey, CMediaViewer::CShared *pShared)
{
	ClearMediaText(State);
	AlbumArtKey.clear();
	ClearSharedAlbumArt(pShared);
}

static void ResetSharedState(CMediaViewer::CShared *pShared, SPlainState &State, bool &HasMedia, std::string &AlbumArtKey)
{
	HasMedia = false;
	State = SPlainState{};
	AlbumArtKey.clear();
	ClearSharedAlbumArt(pShared);
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_State = State;
	pShared->m_HasMedia = false;
}

static void UpdateAlbumArtData(CMediaViewer::CShared *pShared, const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &Thumbnail, const std::atomic_bool &StopFlag)
{
	if(!Thumbnail)
	{
		ClearSharedAlbumArt(pShared);
		return;
	}

	try
	{
		const auto StreamOp = Thumbnail.OpenReadAsync();
		if(!WaitForAsync(StreamOp, StopFlag))
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const auto Stream = StreamOp.GetResults();
		if(!Stream)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		const auto DecoderOp = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(Stream);
		if(!WaitForAsync(DecoderOp, StopFlag))
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const auto Decoder = DecoderOp.GetResults();
		if(!Decoder)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const uint32_t Width = Decoder.PixelWidth();
		const uint32_t Height = Decoder.PixelHeight();
		if(Width == 0 || Height == 0)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		const auto PixelDataOp = Decoder.GetPixelDataAsync(
			winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Rgba8,
			winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied,
			winrt::Windows::Graphics::Imaging::BitmapTransform(),
			winrt::Windows::Graphics::Imaging::ExifOrientationMode::IgnoreExifOrientation,
			winrt::Windows::Graphics::Imaging::ColorManagementMode::DoNotColorManage);
		if(!WaitForAsync(PixelDataOp, StopFlag))
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const auto PixelData = PixelDataOp.GetResults();
		if(!PixelData)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		const auto Pixels = PixelData.DetachPixelData();
		const size_t ExpectedSize = (size_t)Width * (size_t)Height * 4;
		if(Pixels.size() < ExpectedSize)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		std::vector<uint8_t> Copy(Pixels.begin(), Pixels.begin() + ExpectedSize);
		SetSharedAlbumArt(pShared, std::move(Copy), (int)Width, (int)Height);
	}
	catch(const winrt::hresult_error &)
	{
		ClearSharedAlbumArt(pShared);
	}
}

static void ApplyRoundedMask(std::vector<uint8_t> &Pixels, int Width, int Height, float Radius)
{
	if(Pixels.empty() || Width <= 0 || Height <= 0 || Radius <= 0.0f)
		return;

	const float MaxRadius = 0.5f * (float)std::min(Width, Height);
	const float R = std::min(Radius, MaxRadius);
	if(R <= 0.0f)
		return;

	const float Left = R;
	const float Right = (float)Width - R;
	const float Top = R;
	const float Bottom = (float)Height - R;
	const float OuterR2 = R * R;
	const float InnerR = R - 1.0f;
	const float InnerR2 = InnerR > 0.0f ? InnerR * InnerR : 0.0f;
	const bool UseSoftEdge = InnerR > 0.0f;

	for(int y = 0; y < Height; ++y)
	{
		const float Fy = (float)y + 0.5f;
		for(int x = 0; x < Width; ++x)
		{
			const float Fx = (float)x + 0.5f;
			float Dx = 0.0f;
			float Dy = 0.0f;
			bool Corner = false;

			if(Fx < Left && Fy < Top)
			{
				Dx = Left - Fx;
				Dy = Top - Fy;
				Corner = true;
			}
			else if(Fx > Right && Fy < Top)
			{
				Dx = Fx - Right;
				Dy = Top - Fy;
				Corner = true;
			}
			else if(Fx < Left && Fy > Bottom)
			{
				Dx = Left - Fx;
				Dy = Fy - Bottom;
				Corner = true;
			}
			else if(Fx > Right && Fy > Bottom)
			{
				Dx = Fx - Right;
				Dy = Fy - Bottom;
				Corner = true;
			}

			if(!Corner)
				continue;

			const float Dist2 = Dx * Dx + Dy * Dy;
			if(Dist2 <= (UseSoftEdge ? InnerR2 : OuterR2))
				continue;

			float Alpha = 0.0f;
			if(UseSoftEdge && Dist2 < OuterR2)
			{
				const float Dist = std::sqrt(Dist2);
				Alpha = std::clamp(R - Dist, 0.0f, 1.0f);
			}

			const size_t Index = (size_t)(y * Width + x) * 4;
			if(Alpha <= 0.0f)
			{
				Pixels[Index + 0] = 0;
				Pixels[Index + 1] = 0;
				Pixels[Index + 2] = 0;
				Pixels[Index + 3] = 0;
			}
			else if(Alpha < 1.0f)
			{
				Pixels[Index + 0] = (uint8_t)std::round(Pixels[Index + 0] * Alpha);
				Pixels[Index + 1] = (uint8_t)std::round(Pixels[Index + 1] * Alpha);
				Pixels[Index + 2] = (uint8_t)std::round(Pixels[Index + 2] * Alpha);
				Pixels[Index + 3] = (uint8_t)std::round(Pixels[Index + 3] * Alpha);
			}
		}
	}
}

static void ApplySharedAlbumArt(CMediaViewer::CShared *pShared, CMediaViewer::SWinrt *pWinrt, IGraphics *pGraphics)
{
	if(!pShared || !pWinrt || !pGraphics)
		return;

	bool AlbumArtDirty = false;
	int AlbumArtWidth = 0;
	int AlbumArtHeight = 0;
	std::vector<uint8_t> AlbumArtPixels;
	{
		std::scoped_lock Lock(pShared->m_Mutex);
		if(pShared->m_AlbumArtDirty)
		{
			AlbumArtDirty = true;
			AlbumArtWidth = pShared->m_AlbumArtWidth;
			AlbumArtHeight = pShared->m_AlbumArtHeight;
			AlbumArtPixels = std::move(pShared->m_AlbumArtRgba);
			pShared->m_AlbumArtRgba.clear();
			pShared->m_AlbumArtDirty = false;
		}
	}

	if(!AlbumArtDirty)
		return;

	ClearAlbumArtLocal(pWinrt, pGraphics);

	const size_t ExpectedSize = (size_t)AlbumArtWidth * (size_t)AlbumArtHeight * 4;
	if(AlbumArtWidth > 0 && AlbumArtHeight > 0 && AlbumArtPixels.size() >= ExpectedSize)
	{
		const float RoundingRatio = 2.0f / 14.0f;
		const float Radius = (float)std::min(AlbumArtWidth, AlbumArtHeight) * RoundingRatio;
		ApplyRoundedMask(AlbumArtPixels, AlbumArtWidth, AlbumArtHeight, Radius);

		CImageInfo Image;
		Image.m_Width = (size_t)AlbumArtWidth;
		Image.m_Height = (size_t)AlbumArtHeight;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(ExpectedSize));
		if(Image.m_pData)
		{
			mem_copy(Image.m_pData, AlbumArtPixels.data(), ExpectedSize);
			pWinrt->m_State.m_AlbumArt = pGraphics->LoadTextureRawMove(Image, 0, "smtc_album_art");
			if(pWinrt->m_State.m_AlbumArt.IsValid())
			{
				pWinrt->m_State.m_AlbumArtWidth = AlbumArtWidth;
				pWinrt->m_State.m_AlbumArtHeight = AlbumArtHeight;
			}
		}
	}
}
#endif

CMediaViewer::CMediaViewer() = default;
CMediaViewer::~CMediaViewer() = default;

#if MEDIA_PLAYER_WINRT
void CMediaViewer::ThreadMain()
{
	try
	{
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	}
	catch(const winrt::hresult_error &)
	{
		return;
	}

	{
		// Release WinRT objects before tearing down the apartment.
		GlobalSystemMediaTransportControlsSessionManager Manager{nullptr};
		GlobalSystemMediaTransportControlsSession Session{nullptr};
		SPlainState State{};
		bool HasMedia = false;
		std::string AlbumArtKey;
		auto LastPropsUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(2);

		while(!m_StopThread)
		{
			try
			{
				if(!Manager)
				{
					try
					{
						const auto RequestOp = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
						if(!WaitForAsync(RequestOp, m_StopThread))
						{
							if(m_StopThread.load(std::memory_order_relaxed))
								break;
							Manager = nullptr;
						}
						else
						{
							Manager = RequestOp.GetResults();
						}
					}
					catch(const winrt::hresult_error &)
					{
						Manager = nullptr;
					}
				}

				if(!Manager)
				{
					if(HasMedia)
					{
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				Session = Manager.GetCurrentSession();
				if(!Session)
				{
					if(HasMedia)
					{
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}

				const auto PlaybackInfo = Session.GetPlaybackInfo();
				if(!PlaybackInfo)
				{
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				const auto Controls = PlaybackInfo.Controls();
				if(!Controls)
				{
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				State.m_CanPlay = Controls.IsPlayEnabled();
				State.m_CanPause = Controls.IsPauseEnabled();
				State.m_CanPrev = Controls.IsPreviousEnabled();
				State.m_CanNext = Controls.IsNextEnabled();
				State.m_Playing = PlaybackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

				const auto Timeline = Session.GetTimelineProperties();
				if(!Timeline)
				{
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				const int64_t Start100ns = Timeline.StartTime().count();
				const int64_t End100ns = Timeline.EndTime().count();
				const int64_t Position100ns = Timeline.Position().count();
				const int64_t Duration100ns = End100ns - Start100ns;
				const int64_t PositionRel100ns = Position100ns - Start100ns;
				State.m_DurationMs = Duration100ns > 0 ? Duration100ns / 10000 : 0;
				State.m_PositionMs = PositionRel100ns > 0 ? PositionRel100ns / 10000 : 0;
				HasMedia = true;

				const auto Now = std::chrono::steady_clock::now();
				if(Now - LastPropsUpdate >= std::chrono::seconds(1))
				{
					LastPropsUpdate = Now;
					try
					{
						const auto MediaPropsOp = Session.TryGetMediaPropertiesAsync();
						if(!WaitForAsync(MediaPropsOp, m_StopThread))
						{
							if(m_StopThread.load(std::memory_order_relaxed))
								break;
							ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
						}
						else
						{
							const auto MediaProps = MediaPropsOp.GetResults();
							if(!MediaProps)
							{
								ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
							}
							else
							{
								const std::string ServiceId = Session ? winrt::to_string(Session.SourceAppUserModelId()) : std::string();
								const std::string Title = winrt::to_string(MediaProps.Title());
								const std::string Artist = winrt::to_string(MediaProps.Artist());
								const std::string Album = winrt::to_string(MediaProps.AlbumTitle());

								if(!ServiceId.empty())
								{
									State.m_ServiceId = ServiceId;
								}
								else
								{
									State.m_ServiceId.clear();
								}

								if(!Title.empty())
								{
									State.m_Title = Title;
								}
								else
								{
									State.m_Title.clear();
								}

								if(!Artist.empty())
								{
									State.m_Artist = Artist;
								}
								else
								{
									State.m_Artist.clear();
								}

								if(!Album.empty())
								{
									State.m_Album = Album;
								}
								else
								{
									State.m_Album.clear();
								}

								const bool HasText = !Title.empty() || !Artist.empty() || !Album.empty();
								if(HasText)
								{
									const std::string NewKey = Title + "\n" + Artist + "\n" + Album;
									if(NewKey != AlbumArtKey)
									{
										AlbumArtKey = NewKey;
										const auto Thumbnail = MediaProps.Thumbnail();
										if(Thumbnail)
											UpdateAlbumArtData(m_pShared.get(), Thumbnail, m_StopThread);
										else
											ClearSharedAlbumArt(m_pShared.get());
									}
								}
								else
								{
									ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
								}
							}
						}
					}
					catch(const winrt::hresult_error &)
					{
						ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
					}
				}

				{
					std::scoped_lock Lock(m_pShared->m_Mutex);
					m_pShared->m_State = State;
					m_pShared->m_HasMedia = HasMedia;
				}

				std::deque<ECommand> Commands;
				{
					std::scoped_lock Lock(m_pShared->m_Mutex);
					Commands.swap(m_pShared->m_Commands);
				}
				if(Session)
				{
					for(const auto Command : Commands)
					{
						try
						{
							switch(Command)
							{
							case ECommand::Prev:
								Session.TrySkipPreviousAsync();
								break;
							case ECommand::PlayPause:
								Session.TryTogglePlayPauseAsync();
								break;
							case ECommand::Next:
								Session.TrySkipNextAsync();
								break;
							}
						}
						catch(const winrt::hresult_error &)
						{
						}
					}
				}
			}

			catch(const winrt::hresult_error &)
			{
				ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
			}
			catch(...)
			{
				ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}

	winrt::uninit_apartment();
}

void CMediaViewer::AudioThreadMain()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	IMMDeviceEnumerator *pEnumerator = nullptr;
	IMMDevice *pDevice = nullptr;
	IAudioClient *pAudioClient = nullptr;
	IAudioCaptureClient *pCaptureClient = nullptr;

	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		nullptr,
		CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void **)&pEnumerator);

	if(SUCCEEDED(hr))
	{
		// Get default audio endpoint (speakers/headphones - loopback capture)
		hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	}

	if(SUCCEEDED(hr))
	{
		hr = pDevice->Activate(
			__uuidof(IAudioClient),
			CLSCTX_ALL,
			nullptr,
			(void **)&pAudioClient);
	}

	WAVEFORMATEX *pWaveFormat = nullptr;
	if(SUCCEEDED(hr))
	{
		hr = pAudioClient->GetMixFormat(&pWaveFormat);
	}

	if(SUCCEEDED(hr))
	{
		// Initialize audio client in loopback mode
		hr = pAudioClient->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_LOOPBACK,
			10000000, // 1 second buffer
			0,
			pWaveFormat,
			nullptr);
	}

	if(SUCCEEDED(hr))
	{
		hr = pAudioClient->GetService(
			__uuidof(IAudioCaptureClient),
			(void **)&pCaptureClient);
	}

	if(SUCCEEDED(hr))
	{
		hr = pAudioClient->Start();
	}

	const int SampleRate = pWaveFormat ? pWaveFormat->nSamplesPerSec : 48000;
	const int Channels = pWaveFormat ? pWaveFormat->nChannels : 2;

	std::vector<float> SampleBuffer;
	SampleBuffer.reserve(FFT::FFT_SIZE);

	while(!m_StopAudioThread && SUCCEEDED(hr))
	{
		UINT32 PacketLength = 0;
		hr = pCaptureClient->GetNextPacketSize(&PacketLength);

		while(SUCCEEDED(hr) && PacketLength != 0)
		{
			BYTE *pData = nullptr;
			UINT32 NumFramesAvailable = 0;
			DWORD Flags = 0;

			hr = pCaptureClient->GetBuffer(
				&pData,
				&NumFramesAvailable,
				&Flags,
				nullptr,
				nullptr);

			if(SUCCEEDED(hr))
			{
				if(!(Flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData)
				{
					// Convert to mono float samples
					const float *pFloatData = reinterpret_cast<const float *>(pData);
					for(UINT32 i = 0; i < NumFramesAvailable; ++i)
					{
						float sample = 0.0f;
						for(int ch = 0; ch < Channels; ++ch)
						{
							sample += pFloatData[i * Channels + ch];
						}
						sample /= (float)Channels;

						SampleBuffer.push_back(sample);

						// Process when we have enough samples
						if(SampleBuffer.size() >= FFT::FFT_SIZE)
						{
							ProcessAudioFrame(SampleBuffer.data(), SampleBuffer.size(), SampleRate);
							SampleBuffer.clear();
						}
					}
				}

				pCaptureClient->ReleaseBuffer(NumFramesAvailable);
			}

			hr = pCaptureClient->GetNextPacketSize(&PacketLength);
		}
		// sleep for 1ms
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	if(pAudioClient)
		pAudioClient->Stop();

	if(pCaptureClient)
		pCaptureClient->Release();
	if(pAudioClient)
		pAudioClient->Release();
	if(pDevice)
		pDevice->Release();
	if(pEnumerator)
		pEnumerator->Release();
	if(pWaveFormat)
		CoTaskMemFree(pWaveFormat);

	CoUninitialize();
}

void CMediaViewer::ProcessAudioFrame(const float *pSamples, int NumSamples, int SampleRate)
{
	std::vector<FFT::CComplex> fftResult;
	FFT::ComputeFFT(pSamples, NumSamples, fftResult);

	// Convert FFT bins to frequency bands (logarithmic scale sounds better)
	const int NumBands = CVisualizer::NUM_FREQUENCY_BANDS;
	std::array<float, CVisualizer::NUM_FREQUENCY_BANDS> bands{};

	const int UsableBins = FFT::FFT_SIZE / 2; // Only use first half (Nyquist)
	const float FreqPerBin = (float)SampleRate / FFT::FFT_SIZE;

	// Logarithmic frequency mapping
	const float MinFreq = 48000.0f / FFT::FFT_SIZE; // Calculate at 48kHz sampling rate
	const float MaxFreq = 20000.0f; // 20 kHz
	const float LogMin = std::log10(MinFreq);
	const float LogMax = std::log10(MaxFreq);

	bool AllZero = true;
	for(int band = 0; band < NumBands; ++band)
	{
		const float t0 = (float)band / NumBands;
		const float t1 = (float)(band + 1) / NumBands;
		const float freq0 = std::pow(10.0f, LogMin + t0 * (LogMax - LogMin));
		const float freq1 = std::pow(10.0f, LogMin + t1 * (LogMax - LogMin));

		const int bin0 = std::clamp((int)(freq0 / FreqPerBin), 0, UsableBins - 1);
		const int bin1 = std::clamp((int)(freq1 / FreqPerBin), bin0 + 1, UsableBins);

		// Average magnitude in this frequency range
		float sum = 0.0f;
		int count = 0;
		for(int bin = bin0; bin < bin1; ++bin)
		{
			sum += fftResult[bin].Magnitude();
			++count;
		}

		if(count > 0)
		{
			float magnitude = sum / count;
			// Apply logarithmic scaling and normalize
			magnitude = std::log10(1.0f + magnitude * 100.0f) / 2.0f;
			bands[band] = std::clamp(magnitude, 0.0f, 1.0f);
			if(magnitude > 0.001f)
				AllZero = false;
		}
	}

	// Smooth with previous values
	{
		std::scoped_lock lock(m_pAudioCapture->m_Mutex);
		const float smoothing = 0.7f; // Higher = smoother
		for(int i = 0; i < NumBands; ++i)
		{
			m_pAudioCapture->m_aFrequencyBands[i] =
				m_pAudioCapture->m_aFrequencyBands[i] * smoothing +
				bands[i] * (1.0f - smoothing);
		}
		m_pAudioCapture->m_Active = true;

		if(!AllZero)
			m_pAudioCapture->m_LastFrequencyChange = time_get();

		const int TimeoutSec = 10;
		const int64_t Now = time_get();
		const int64_t Elapsed = (m_pAudioCapture->m_LastFrequencyChange == 0) ? INT64_MAX : (Now - m_pAudioCapture->m_LastFrequencyChange);

		// If we've never seen audio (last == 0) or elapsed time exceeds timeout, deactivate
		if(m_pAudioCapture->m_LastFrequencyChange == 0 || Elapsed > time_freq() * TimeoutSec)
		{
			m_pAudioCapture->m_Active = false;
		}
	}
}
#endif

void CMediaViewer::OnInit()
{
#if MEDIA_PLAYER_WINRT
	m_pWinrt = std::make_unique<SWinrt>();
	m_pShared = std::make_unique<CShared>();
	m_pAudioCapture = std::make_unique<CAudioCapture>();

	m_StopThread.store(false, std::memory_order_relaxed);
	m_StopAudioThread.store(false, std::memory_order_relaxed);

	m_Thread = std::thread(&CMediaViewer::ThreadMain, this);
	m_AudioThread = std::thread(&CMediaViewer::AudioThreadMain, this);
#endif
}

void CMediaViewer::OnShutdown()
{
#if MEDIA_PLAYER_WINRT
	m_StopThread.store(true, std::memory_order_relaxed);
	m_StopAudioThread.store(true, std::memory_order_relaxed);

	if(m_Thread.joinable())
		m_Thread.join();
	if(m_AudioThread.joinable())
		m_AudioThread.join();

	ClearState(m_pWinrt.get(), Graphics());

	m_pAudioCapture.reset();
	m_pShared.reset();
	m_pWinrt.reset();
#endif
}

void CMediaViewer::OnUpdate()
{
#if MEDIA_PLAYER_WINRT
	if(!m_pWinrt)
		return;

	if(!m_pShared)
		return;

	SPlainState SharedState{};
	bool HasMedia = false;
	{
		std::scoped_lock Lock(m_pShared->m_Mutex);
		SharedState = m_pShared->m_State;
		HasMedia = m_pShared->m_HasMedia;
	}

	if(!HasMedia)
	{
		if(m_pWinrt->m_HasMedia)
			ClearState(m_pWinrt.get(), Graphics());
		m_pWinrt->m_HasMedia = false;
	}
	else
	{
		m_pWinrt->m_HasMedia = true;
		m_pWinrt->m_State.m_CanPlay = SharedState.m_CanPlay;
		m_pWinrt->m_State.m_CanPause = SharedState.m_CanPause;
		m_pWinrt->m_State.m_CanPrev = SharedState.m_CanPrev;
		m_pWinrt->m_State.m_CanNext = SharedState.m_CanNext;
		m_pWinrt->m_State.m_Playing = SharedState.m_Playing;
		m_pWinrt->m_State.m_ServiceId = SharedState.m_ServiceId;
		m_pWinrt->m_State.m_Title = SharedState.m_Title;
		m_pWinrt->m_State.m_Artist = SharedState.m_Artist;
		m_pWinrt->m_State.m_Album = SharedState.m_Album;
		m_pWinrt->m_State.m_PositionMs = SharedState.m_PositionMs;
		m_pWinrt->m_State.m_DurationMs = SharedState.m_DurationMs;
	}

	ApplySharedAlbumArt(m_pShared.get(), m_pWinrt.get(), Graphics());
#endif
}

bool CMediaViewer::GetStateSnapshot(CMediaViewer::CState &State) const
{
#if MEDIA_PLAYER_WINRT
	if(!m_pShared || !m_pWinrt || !m_pAudioCapture)
	{
		State = CMediaViewer::CState{};
		return false;
	}

	if(!g_Config.m_ClShowMediaPlayer)
	{
		State = CMediaViewer::CState{};
		return false;
	}

	if(m_pWinrt && m_pWinrt->m_HasMedia)
	{
		State = m_pWinrt->m_State;
		/* Visualizer Data */
		{
			std::scoped_lock lock(m_pAudioCapture->m_Mutex);
			State.m_Visualizer.m_aFrequencyBands = m_pAudioCapture->m_aFrequencyBands;
			State.m_Visualizer.m_Active = m_pAudioCapture->m_Active;
			State.m_Visualizer.m_LastFrequencyChange = m_pAudioCapture->m_LastFrequencyChange;
		}
		return true;
	}
#endif

	State = CMediaViewer::CState{};
	return false;
}

void CMediaViewer::Previous()
{
#if MEDIA_PLAYER_WINRT
	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::Prev);
#endif
}

void CMediaViewer::PlayPause()
{
#if MEDIA_PLAYER_WINRT
	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::PlayPause);
#endif
}

void CMediaViewer::Next()
{
#if MEDIA_PLAYER_WINRT
	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::Next);
#endif
}

void CMediaViewer::CVisualizer::GetBands(float *pOutBands, int NumBands) const
{
	const int CopyCount = std::min(NumBands, NUM_FREQUENCY_BANDS);
	for(int i = 0; i < CopyCount; ++i)
	{
		pOutBands[i] = m_aFrequencyBands[i];
	}
}

float CMediaViewer::CVisualizer::GetAverageBand() const
{
	float Sum = 0.0f;
	for(float Band : m_aFrequencyBands)
	{
		Sum += Band;
	}
	return Sum / NUM_FREQUENCY_BANDS;
}
