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

	/**
	 * A turn of the camera made with the mouse until the camera faces where it should, not by a fixed
	 * amount of mouse movement: how far a count of mouse movement turns the camera is not the same
	 * from one run to the next.
	 */
	struct FMouseTurn
	{
		/** One step of a turn of Degrees from where the camera faced on the first step. True once it is made. */
		bool Step(APlayerController* Controller, double Degrees)
		{
			if (bDone) {
				return true;
			}
			if (!bBegun) {
				bBegun = true;
				Target = Controller->GetControlRotation().Yaw + Degrees;
				Sign = Degrees < 0.0 ? -1.0 : 1.0;
			}
			double Error = FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Target);
			if (FMath::Abs(Error) < 0.4) {
				bDone = true;
				return true;
			}
			// a half turn can be read as either way round at its start: keep going the way asked
			if (FMath::Abs(Error) > 170.0) {
				Error = Sign * FMath::Abs(Error);
			}
			Mouse(Controller, EKeys::MouseX, FMath::Clamp(Error / 0.25, -40.0, 40.0));
			return false;
		}
		void Reset() { bBegun = false; bDone = false; }

	private:
		bool bBegun = false;
		bool bDone = false;
		double Target = 0.0;
		double Sign = 1.0;
	};

	/**
	 * One step of turning the camera with the mouse, sideways and up or down, until a shot leaving
	 * Muzzle along the way the camera faces passes through Target. True once it does, to within
	 * Tolerance degrees.
	 */
	inline bool AimStep(APlayerController* Controller, const FVector& Muzzle, const FVector& Target, double Tolerance = 0.2)
	{
		const FRotator Want = (Target - Muzzle).Rotation();
		const FRotator Have = Controller->GetControlRotation();
		const double Yaw = FMath::FindDeltaAngleDegrees(Have.Yaw, Want.Yaw);
		const double Pitch = FMath::FindDeltaAngleDegrees(Have.Pitch, Want.Pitch);
		if (FMath::Abs(Yaw) < Tolerance && FMath::Abs(Pitch) < Tolerance) {
			return true;
		}
		Mouse(Controller, EKeys::MouseX, FMath::Clamp(Yaw / 0.25, -40.0, 40.0));
		Mouse(Controller, EKeys::MouseY, FMath::Clamp(Pitch / 0.25, -40.0, 40.0));
		return false;
	}

	/** Save what the player sees, HUD included, to Saved/<Folder>/<Name>. Does nothing in a run that cannot draw. */
	inline void Shot(const TCHAR* Folder, const TCHAR* Name)
	{
		if (FApp::CanEverRender()) {
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / Folder / Name, true, false);
		}
	}
}
