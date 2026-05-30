// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"


WWISEMOVIERENDEROUTPUT_API DECLARE_LOG_CATEGORY_EXTERN(LogWwiseMovieRenderOutput, Log, All);


class FWwiseMovieRenderOutputModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
