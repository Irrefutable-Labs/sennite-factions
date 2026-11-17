// What the menu and lobby checks share: the menu's own controller and HUD in the running game, and a
// tap on one of its buttons where the last frame drew it, through the game's viewport (GameCheck::Tap).

#pragma once

#include "CoreMinimal.h"
#include "GameCheck.h"
#include "SenniteMenu.h"

namespace MenuCheck
{
	inline ASenniteMenuController* Menu(UWorld* World)
	{
		return World != nullptr ? Cast<ASenniteMenuController>(World->GetFirstPlayerController()) : nullptr;
	}

	inline ASenniteMenuHUD* Hud(ASenniteMenuController* Controller)
	{
		return Controller != nullptr ? Cast<ASenniteMenuHUD>(Controller->GetHUD()) : nullptr;
	}

	/** Tap the button the last frame drew for Action. False if it drew none. */
	inline bool Tap(ASenniteMenuController* Controller, FName Action)
	{
		FVector2D Where;
		const ASenniteMenuHUD* Drawn = Hud(Controller);
		if (Drawn == nullptr || !Drawn->Centre(Action, Where)) {
			return false;
		}
		GameCheck::Tap(Where);
		return true;
	}

	/** Whether the last frame drew a button for Action, and whether it was enabled. */
	inline bool Has(ASenniteMenuController* Controller, FName Action, bool* bEnabled = nullptr)
	{
		const ASenniteMenuHUD* Drawn = Hud(Controller);
		if (Drawn == nullptr) {
			return false;
		}
		for (const FSenniteMenuButton& Each : Drawn->Buttons) {
			if (Each.Action == Action) {
				if (bEnabled != nullptr) {
					*bEnabled = Each.bEnabled;
				}
				return true;
			}
		}
		return false;
	}

	/** The text the last frame drew under a name ("hero", "status", "player1", ...); empty if none. */
	inline FString Line(ASenniteMenuController* Controller, const TCHAR* Name)
	{
		const ASenniteMenuHUD* Drawn = Hud(Controller);
		const FString Prefix = FString(Name) + TEXT("=");
		if (Drawn != nullptr) {
			for (const FString& Each : Drawn->LastDrawn) {
				if (Each.StartsWith(Prefix)) {
					return Each.Mid(Prefix.Len());
				}
			}
		}
		return FString();
	}
}
