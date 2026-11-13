// What the in-game checks share: finding the running game, pressing the player's real keys and
// mouse on the player controller, and saving a screenshot.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace GameCheck
{
	/** The world of the running game (or of a play session in the editor). */
	inline UWorld* FindGameWorld()
	{
		if (GEngine == nullptr) {
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World() != nullptr) {
				return Context.World();
			}
		}
		return nullptr;
	}

	/** Press or release a key or mouse button, as the player's keyboard and mouse would. */
	inline void Key(APlayerController* Controller, const FKey& InKey, EInputEvent Event)
	{
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, Event, Event == IE_Released ? 0.0f : 1.0f, -1,
			IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()));
	}

	/** Move the mouse along one axis by a number of mouse counts. */
	inline void Mouse(APlayerController* Controller, const FKey& Axis, float Counts)
	{
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Axis, IE_Axis, Counts, 1,
			IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()));
	}

	/** Save what the player sees, HUD included, to Saved/<Folder>/<Name>. Does nothing in a run that cannot draw. */
	inline void Shot(const TCHAR* Folder, const TCHAR* Name)
	{
		if (FApp::CanEverRender()) {
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / Folder / Name, true, false);
		}
	}
}
