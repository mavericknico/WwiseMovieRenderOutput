#include "WwiseMoviePipelineWaveOutput.h"
#include "WwiseMovieRenderOutput.h"
#include "MoviePipeline.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePrimaryConfig.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "Wwise/WwiseSoundEngineUtils.h"


namespace
{
	static TArray<uint8> FStringToBytes(const FString& String)
	{
		TArray<uint8> OutBytes;

		if (!String.IsEmpty())
		{
			FTCHARToUTF8 Converted(*String);
			OutBytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		}

		return OutBytes;
	}

	TArray<uint8> U32ToBytes(const uint32 Value)
	{
		TArray<uint8> OutBytes;

		OutBytes.Add(Value >> 0 & 0xFF);
		OutBytes.Add(Value >> 8 & 0xFF);
		OutBytes.Add(Value >> 16 & 0xFF);
		OutBytes.Add(Value >> 24 & 0xFF);

		return OutBytes;
	}

	TArray<uint8> U16ToBytes(const uint16 Value)
	{
		TArray<uint8> OutBytes;

		OutBytes.Add(Value >> 0 & 0xFF);
		OutBytes.Add(Value >> 8 & 0xFF);
		return OutBytes;}

	TArray<uint8> FloatToPCM24(const float Value)
	{
		TArray<uint8> OutBytes;
		OutBytes.Reserve(3);

		static constexpr float scale = 8388607.0f; // 2^23 - 1
		static constexpr int32 min24 = -8388608;
		static constexpr int32 max24 =  8388607;

		const float s = FMath::IsNaN(Value) ? 0.f : FMath::Clamp(Value, -1.f, 1.f);
		int64 v = static_cast<int64>(::lrintf(s * scale));
		if (v < min24) v = min24;
		if (v > max24) v = max24;

		OutBytes.Add(static_cast<uint8_t>( v & 0xFF));
		OutBytes.Add(static_cast<uint8_t>((v >> 8)  & 0xFF));
		OutBytes.Add(static_cast<uint8_t>((v >> 16) & 0xFF));

		return OutBytes;
	}
	
	static bool ExportAudioDataToFile(const TArray<uint8>& AudioBytes, int SampleRate, int Channels, const FString& FilePath, int BitDepth=24)
	{
		checkf(!AudioBytes.IsEmpty() && SampleRate > 0 && Channels > 0 && !FilePath.IsEmpty(), TEXT("Invalid audio data to render as wav file"));

		TArray<uint8> AudioHeader;
		constexpr uint16 Format = 1;			// PCM
		constexpr uint16 FormatLength = 16;
		constexpr uint16 HeaderLength = 44;

		// Reference: https://docs.fileformat.com/audio/wav/						 Bytes
		AudioHeader.Append(FStringToBytes("RIFF"));									// 1-4
		AudioHeader.Append(U32ToBytes(AudioBytes.Num() + HeaderLength));		// 5-8
		AudioHeader.Append(FStringToBytes("WAVEfmt "));								// 9-16
		AudioHeader.Append(U32ToBytes(FormatLength));								// 17-20
		AudioHeader.Append(U16ToBytes(Format));										// 21-22
		AudioHeader.Append(U16ToBytes(Channels));									// 23-24
		AudioHeader.Append(U32ToBytes(SampleRate));									// 25-28
		AudioHeader.Append(U32ToBytes(SampleRate * BitDepth * Channels / 8));	// 29-32
		AudioHeader.Append(U16ToBytes(BitDepth * Channels / 8));				// 33-34
		AudioHeader.Append(U16ToBytes(BitDepth));									// 35-36
		AudioHeader.Append(FStringToBytes("data"));									// 37-40
		AudioHeader.Append(U32ToBytes(AudioBytes.Num()));						// 41-44

		TArray<uint8> OutBytes;
		OutBytes.Append(AudioHeader);
		OutBytes.Append(AudioBytes);

		FFileHelper::SaveArrayToFile(OutBytes, *FilePath);

		return true;
	}
	
	static TArray<uint8> AudioBufferToPCM24(AkAudioBuffer& InAudioBuffer)
	{
		TArray<uint8> OutBytes;

		const float *AudioData = static_cast<float*>(InAudioBuffer.GetInterleavedData());
		const size_t NumSamples = InAudioBuffer.NumChannels() * InAudioBuffer.uValidFrames;
		OutBytes.Reserve(NumSamples * 3);	// reserve more space

		for (size_t i = 0; i < NumSamples; ++i)
		{
			// Convert interleaved float [-1,1] to signed 24-bit PCM (packed 3-byte, little-endian)
			OutBytes.Append(FloatToPCM24(AudioData[i]));
		}

		return OutBytes;
	}
}


UWwiseMoviePipelineWaveOutput::UWwiseMoviePipelineWaveOutput()
: SampleRate(0), Channels(0), OutputDeviceId(AK_INVALID_OUTPUT_DEVICE_ID), bIsCapturing(false)
{
}

void UWwiseMoviePipelineWaveOutput::OnReceiveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame)
{
	Super::OnReceiveImageDataImpl(InMergedOutputFrame);

	// this is the callback from MRQ when rendering a frame.

	if (!InMergedOutputFrame)
	{
		return;
	}

	// Tell Wwise to render this frame of audio, which results in `CaptureCallback` getting called back
	// with the rendered audio buffer for converting to 24-bit PCM and saving until the end when we'll generate
	// the output wav file.
	
	IWwiseSoundEngineAPI* const SoundEngine = IWwiseSoundEngineAPI::Get();
	if (SoundEngine && SoundEngine->IsInitialized())
	{
		// specify frame time for this frame ...
		const float DeltaTime = InMergedOutputFrame->FrameOutputState.TimeData.FrameDeltaTime;
		SoundEngine->SetOfflineRenderingFrameTime(DeltaTime);

		// ... and tell wwise to render the frame
		SoundEngine->RenderAudio();
	}
}

void UWwiseMoviePipelineWaveOutput::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
	check(InPipeline);

	IWwiseSoundEngineAPI* const SoundEngine = IWwiseSoundEngineAPI::Get();
	if (!SoundEngine->IsInitialized())
	{
		UE_LOG(LogWwiseMovieRenderOutput, Error, TEXT("UWwiseMoviePipelineWaveOutput::SetupForPipelineImpl - sound engine not initialized - initializing now!"));
		return;
	}

	if (UMoviePipelineOutputSetting* OutputSetting = InPipeline->GetPipelinePrimaryConfig()->FindSetting<UMoviePipelineOutputSetting>())
	{
		FString Filename = OverrideOutputFileName.IsNone() ? OutputSetting->FileNameFormat : OverrideOutputFileName.ToString();
		FString FileNameFormatString = OutputSetting->OutputDirectory.Path / Filename;
		
		// Generate a filename for this encoded file
		TMap<FString, FString> FormatOverrides;
		FormatOverrides.Add(TEXT("ext"), TEXT("wav"));
		FMoviePipelineFormatArgs FinalFormatArgs;
		
		InPipeline->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, OutputPath, FinalFormatArgs);

		// OutputPath = FPaths::Combine( *OutputSetting->OutputDirectory.Path, *OutputFileName.ToString());
		UE_LOG(LogWwiseMovieRenderOutput, Verbose, TEXT("Output Directory: %s, FileNameFormat: %s, OutputPath: %s"),
			*OutputSetting->OutputDirectory.Path, *OutputSetting->FileNameFormat, *OutputPath);
	}
	else
	{
		UE_LOG(LogWwiseMovieRenderOutput, Error, TEXT("UWwiseMoviePipelineWaveOutput::SetupForPipelineImpl - Could not retrieve output settings. No wwise audio output will be generated."));
	}

	// get current channel config
	AkChannelConfig ChannelConfig { SoundEngine->GetSpeakerConfiguration(0) };
	SampleRate = SoundEngine->GetSampleRate();

	// if no default channel config set for whatever reason, fallback to standard stereo mix
	if (ChannelConfig.uNumChannels <= 0)
	{
		ChannelConfig.SetStandard(AK_SPEAKER_SETUP_2_0);
	}
	Channels = ChannelConfig.uNumChannels;
	
	// set wwise to offline rendering mode
	UE_LOG(LogWwiseMovieRenderOutput, Log, TEXT("Setting Wwise to offline rendering mode"));
	AKRESULT Result = SoundEngine->SetOfflineRendering(true);
	if ( AK_Success != Result)
	{
		UE_LOG(LogWwiseMovieRenderOutput, Error, TEXT("UWwiseMoviePipelineWaveOutput::SetupForPipelineImpl - unable to set Wwise offline rendering mode (%d)"), Result);
		return;
	}

	// flush the message queue w/o rendering any data. Offline rendering enabled after this.
	SoundEngine->SetOfflineRenderingFrameTime(0.0f);
	SoundEngine->RenderAudio();

	// register callback to receive rendered audio buffers
	UE_LOG(LogWwiseMovieRenderOutput, Log, TEXT("Registering Wwise offline rendering capture callback"));
	Result = SoundEngine->RegisterCaptureCallback(&CaptureCallback, OutputDeviceId, this);
	if ( AK_Success != Result)
	{
		UE_LOG(LogWwiseMovieRenderOutput, Error, TEXT("UWwiseMoviePipelineWaveOutput::SetupForPipelineImpl - CaptureCallback failed to register audio render callback (%d)"), Result);
	}

	bIsCapturing = true;
	
	Super::SetupForPipelineImpl(InPipeline);
}

void UWwiseMoviePipelineWaveOutput::OnPipelineFinishedImpl()
{
	bIsCapturing = false;

	if (IWwiseSoundEngineAPI* const SoundEngine = IWwiseSoundEngineAPI::Get(); SoundEngine && SoundEngine->IsInitialized())
	{
		// stop rendering audio
		UE_LOG(LogWwiseMovieRenderOutput, Log, TEXT(__FUNCTION__ " - unregistering wwise capture callback and disabling offline rendering"));
		AKRESULT Result = SoundEngine->UnregisterCaptureCallback(&CaptureCallback, OutputDeviceId, this);
		if (Result != AK_Success)
		{
			UE_LOG(LogWwiseMovieRenderOutput, Warning, TEXT("Error unregistering callback: %s"), WwiseUnrealHelper::GetResultString(Result));
		}
		else
		{
			UE_LOG(LogWwiseMovieRenderOutput, Log, TEXT("Wwise offline rendering capture callback unregistered"));
		}

		Result = SoundEngine->SetOfflineRendering(false);
		if (Result != AK_Success)
		{
			UE_LOG(LogWwiseMovieRenderOutput, Warning, TEXT("Error disabling offline rendering: %s"), WwiseUnrealHelper::GetResultString(Result));
		}
		else
		{
			UE_LOG(LogWwiseMovieRenderOutput, Log, TEXT("Wwise offline rendering disabled"));
			
		}
	}
	
	if (!OutputPath.IsEmpty())
	{
		// save pcm data to wave file format
		UE_LOG(LogWwiseMovieRenderOutput, Log, TEXT(__FUNCTION__ " - writing wav file data to file: %s"), *OutputPath);
		if (!ExportAudioDataToFile(CaptureBuffer, SampleRate, Channels, OutputPath))
		{
			UE_LOG(LogWwiseMovieRenderOutput, Error, TEXT("Failed to render wwise audio offline to %s"), *OutputPath);
		}
	}
	
	// clear pcm data
	CaptureBuffer.Empty();
	
	Super::OnPipelineFinishedImpl();
}

void UWwiseMoviePipelineWaveOutput::CaptureCallback(AkAudioBuffer& in_CaptureBuffer, AkOutputDeviceID in_idOutput,
	void* in_pCookie)
{
	if (in_CaptureBuffer.uValidFrames > 0 && in_pCookie)
	{
		if (UWwiseMoviePipelineWaveOutput* const OutputNode = static_cast<UWwiseMoviePipelineWaveOutput*>(in_pCookie); IsValid(OutputNode))
		{
			if (OutputNode->bIsCapturing)
			{
				// convert to 24-bit PCM and save it for writing out to file later
				OutputNode->CaptureBuffer.Append(AudioBufferToPCM24(in_CaptureBuffer));
			}
		}
	}
}
