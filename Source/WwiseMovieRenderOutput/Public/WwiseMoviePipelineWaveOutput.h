// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CoreMinimal.h"
#include <AK/SoundEngine/Common/AkTypedefs.h>
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include "HAL/ThreadSafeCounter.h"
#include "MoviePipelineOutputBase.h"
#include "WwiseMoviePipelineWaveOutput.generated.h"

/**
 * Wwise Movie Render WAV Output 
 */
UCLASS(BlueprintType)
class WWISEMOVIERENDEROUTPUT_API UWwiseMoviePipelineWaveOutput : public UMoviePipelineOutputBase
{
	GENERATED_BODY()
public:
	UWwiseMoviePipelineWaveOutput();
	virtual FText GetDisplayText() const override { return FText::FromString(TEXT("Wwise .wav Output")); };
	virtual bool IsEditorOnly() const override { return true; };

protected:
	virtual void OnReceiveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame) override;
	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline) override;
	virtual void OnPipelineFinishedImpl() override;

private:
	static void CaptureCallback(AkAudioBuffer& in_CaptureBuffer, AkOutputDeviceID in_idOutput, void* in_pCookie);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName OverrideOutputFileName;
	
private:
	UPROPERTY(Transient)
	TArray<uint8> CaptureBuffer;

	int SampleRate;
	int Channels;

	AkOutputDeviceID OutputDeviceId;
	FThreadSafeBool bIsCapturing;
	FString OutputPath;
};


