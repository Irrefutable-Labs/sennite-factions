// What the in-game checks share: finding the running game, pressing the player's real keys and
// mouse on the player controller, and saving a screenshot.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HealingActor.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "SennitePlayerController.h"
#include "SenniteTouch.h"
#include "UnrealClient.h"
#include "WeaponActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Materials/Material.h"

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

	/**
	 * A finger touching the screen at a point and lifting there, as a phone's screen reports it: through the
	 * game's viewport, which hands it to the player controller, as it does every real touch.
	 */
	inline void Tap(const FVector2D& Where)
	{
		UGameViewportClient* Client = GEngine != nullptr ? GEngine->GameViewport : nullptr;
		if (Client == nullptr) {
			return;
		}
		const FTouchId Finger(IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), ETouchIndex::Touch1);
		Client->InputTouch(Client->Viewport, Finger, ETouchType::Began, Where, 1.0f, FPlatformTime::Cycles64());
		Client->InputTouch(Client->Viewport, Finger, ETouchType::Ended, Where, 0.0f, FPlatformTime::Cycles64());
	}

	/** Whether this game is played by touch: its controls are on the screen (a phone, or bAlwaysShowTouchInterface). */
	inline bool Touching(APlayerController* Controller)
	{
		ASennitePlayerController* Player = Cast<ASennitePlayerController>(Controller);
		return Player != nullptr && Player->GetTouchControls() != nullptr;
	}

	/**
	 * Press a key and let it go as the player would: played by touch, by tapping the control on the screen
	 * that presses it (false if none on the screen does); otherwise on the keyboard.
	 */
	inline bool Press(APlayerController* Controller, const FKey& InKey)
	{
		ASennitePlayerController* Player = Cast<ASennitePlayerController>(Controller);
		USenniteTouchControls* Touch = Player != nullptr ? Player->GetTouchControls() : nullptr;
		if (Touch == nullptr) {
			Key(Controller, InKey, IE_Pressed);
			Key(Controller, InKey, IE_Released);
			return true;
		}
		FVector2D At;
		if (!Touch->WhereKey(InKey, At)) {
			return false;
		}
		Tap(At);
		return true;
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

	/**
	 * The hero goes to the crate beside its start on the test range and opens it, with the player's
	 * real keys: the walk key held (D for the crate to the right of the first start, A for the one to
	 * the left of the second) until the hero is beside a closed crate, then E.
	 */
	struct FCrateRun
	{
		int32 Stage = 0;
		double Since = 0.0;
		/** Why it could not be done; empty while it can. */
		FString Failure;

		/** One step. True once the crate is open, or once it failed. */
		bool Step(UWorld* World, APlayerController* Controller, const ASenniteCharacter* Hero, const FKey& WalkKey)
		{
			const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
			const double Now = World->GetTimeSeconds();
			const auto Next = [this, Now]() { ++Stage; Since = Now; return false; };
			switch (Stage) {
			case 0:
				Key(Controller, WalkKey, IE_Pressed);
				return Next();
			case 1:
				// well inside the crate's reach, so it is in reach on the server too
				if (ASenniteCrate::Nearest(World, Hero->GetActorLocation(), Rules->Reach + Rules->Across - 80.0f, true) == nullptr) {
					if (Now - Since > 8.0) {
						Key(Controller, WalkKey, IE_Released);
						Failure = TEXT("The hero did not get beside a closed crate in 8 s");
						return true;
					}
					return false;
				}
				Key(Controller, WalkKey, IE_Released);
				return Next();
			case 2:
				if (Now - Since < 0.4) {
					return false;
				}
				Key(Controller, EKeys::E, IE_Pressed);
				Key(Controller, EKeys::E, IE_Released);
				return Next();
			default: {
				const ASenniteCrate* Beside = ASenniteCrate::Nearest(World, Hero->GetActorLocation(), Rules->Reach + Rules->Across, false);
				if (Beside != nullptr && Beside->bOpen) {
					return true;
				}
				if (Now - Since > 5.0) {
					Failure = TEXT("The crate did not open at the key");
					return true;
				}
				return false;
			}
			}
		}
	};

	/**
	 * A camera of the check's own, in front of the hero and to its right, for a picture of what the hero
	 * holds: the player's own camera is behind the hero, where its body hides it.
	 */
	struct FOnlooker
	{
		TWeakObjectPtr<ACameraActor> Camera;

		void Look(UWorld* World, APlayerController* Controller, const ACharacter* Hero)
		{
			if (!Camera.IsValid()) {
				Camera = World->SpawnActor<ACameraActor>();
			}
			if (Camera.IsValid()) {
				const FVector Chest = Hero->GetActorLocation() + FVector(0.0, 0.0, 30.0);
				const FVector From = Chest + Hero->GetActorForwardVector() * 200.0 + Hero->GetActorRightVector() * 150.0 + FVector(0.0, 0.0, 25.0);
				Camera->SetActorLocationAndRotation(From, (Chest - From).Rotation());
				Controller->SetViewTarget(Camera.Get());
			}
		}

		void Back(APlayerController* Controller, AActor* Hero) { Controller->SetViewTarget(Hero); }
	};

	/** How many weapons and healing items are in the world in nobody's hand. Crates hold them, so none should be. */
	inline int32 LyingAbout(UWorld* World)
	{
		int32 Lying = 0;
		for (TActorIterator<AWeaponActor> It(World); It; ++It) {
			Lying += It->Holder == nullptr ? 1 : 0;
		}
		for (TActorIterator<AHealingActor> It(World); It; ++It) {
			Lying += It->Holder == nullptr ? 1 : 0;
		}
		return Lying;
	}

	/** Whether the map has a mesh drawn with a material marked as sky: a phone draws the sky's atmosphere only on one. */
	inline bool HasSkyDome(UWorld* World)
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It) {
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			const UMaterialInterface* Face = Mesh != nullptr && Mesh->GetStaticMesh() != nullptr ? Mesh->GetMaterial(0) : nullptr;
			if (Face != nullptr && Face->GetMaterial() != nullptr && Face->GetMaterial()->bIsSky) {
				return true;
			}
		}
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
