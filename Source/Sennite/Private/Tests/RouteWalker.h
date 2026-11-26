// What the in-game checks that cross an arena share: following a connector's way across with the
// player's real keys and mouse.

#pragma once

#include "GameCheck.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace GameCheck
{
	/** Follows a connector's way across with the keys and the mouse. */
	struct FRouteWalker
	{
		int32 Next = 0;
		bool bForward = false;
		bool bRunning = false;
		int32 WaitingForLane = INDEX_NONE;
		FVector From = FVector::ZeroVector;
		bool bBegun = false;
		int32 Jumps = 0;
		int32 LanesCrossed = 0;
		bool bJumpHeld = false;

		void Forward(APlayerController* Controller, bool bGo, bool bRun)
		{
			if (bGo != bForward) {
				bForward = bGo;
				GameCheck::Key(Controller, EKeys::W, bGo ? IE_Pressed : IE_Released);
			}
			if ((bGo && bRun) != bRunning) {
				bRunning = bGo && bRun;
				GameCheck::Key(Controller, EKeys::LeftShift, bRunning ? IE_Pressed : IE_Released);
			}
		}

		/** Whether a sprint across a lane, begun now, meets no craft. */
		static bool LaneIsClear(const ASenniteArena* Arena, int32 ConnectorIndex, int32 Lane, const FVector& Crossing)
		{
			const double Now = Arena->MatchSeconds();
			const TArray<FArenaCraft>& All = Arena->GetPlan().Craft;
			for (int32 Index = 0; Index < All.Num(); ++Index) {
				const FArenaCraft& Craft = All[Index];
				if (Craft.Connector != ConnectorIndex || Craft.Lane != Lane) {
					continue;
				}
				// as large as the vehicle the craft is: across its lane, and along it
				const FVector Box = Arena->CraftBox(Index);
				const double Where = FVector::DotProduct(Crossing - Craft.LaneStart, Craft.LaneDirection);
				// From a standing start 320 short of the lane's middle, a sprint is in the path of a craft
				// 210 wide from about 0.45 s to about 1.0 s after setting off. A wider one is met sooner,
				// while the sprint is still gathering speed, and left later. A little is added at each end
				// and each side.
				const double Wider = FMath::Max(0.0, Box.X * 0.5 - 105.0);
				for (double Ahead = FMath::Max(0.0, 0.4 - Wider / 300.0); Ahead <= 1.06 + Wider / 500.0; Ahead += 0.03) {
					const double Along = FVector::DotProduct(ASenniteArena::CraftLocation(Craft, Now + Ahead) - Craft.LaneStart, Craft.LaneDirection);
					if (FMath::Abs(Along - Where) < Box.Y * 0.5 + 42.0 + 60.0) {
						return false;
					}
				}
			}
			return true;
		}

		/** One step along the way. True once the last point is reached. */
		bool Step(APlayerController* Controller, ASenniteCharacter* Hero, const ASenniteArena* Arena, int32 ConnectorIndex)
		{
			const FArenaConnector& Connector = Arena->GetPlan().Connectors[ConnectorIndex];
			const TArray<FArenaStep>& Route = Connector.Route;
			if (!bBegun) {
				bBegun = true;
				From = Hero->GetActorLocation();
			}
			if (Next >= Route.Num()) {
				Forward(Controller, false, false);
				return true;
			}
			const FVector Here = Hero->GetActorLocation();
			if (bJumpHeld && Hero->GetCharacterMovement()->IsFalling()) {
				bJumpHeld = false;
				GameCheck::Key(Controller, EKeys::SpaceBar, IE_Released);
			}
			// a point to jump from is passed through on the way to the landing after it
			const bool bTakeOff = Route[Next].bJump && Next + 1 < Route.Num();
			const FVector Target = bTakeOff ? Route[Next + 1].Point : Route[Next].Point;

			// reached: the hero has passed the point, measured along the way to it
			const FVector Leg = (Route[Next].Point - From).GetSafeNormal2D();
			const double LegLength = FVector::Dist2D(Route[Next].Point, From);
			const bool bReached = FVector::DotProduct(Here - From, Leg) >= LegLength - (bTakeOff ? 0.0 : 25.0) || FVector::Dist2D(Here, Route[Next].Point) < 30.0;
			if (bReached) {
				if (bTakeOff) {
					// held until she is off the ground: a press let go in the same frame is never seen
					GameCheck::Key(Controller, EKeys::SpaceBar, IE_Pressed);
					bJumpHeld = true;
					++Jumps;
				}
				if (Route[Next].Lane != INDEX_NONE) {
					WaitingForLane = Route[Next].Lane;
				}
				From = Route[Next].Point;
				++Next;
				return false;
			}

			// face the target with the mouse
			const double Want = (Target - Here).Rotation().Yaw;
			const double Error = FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Want);
			if (FMath::Abs(Error) > 0.5) {
				GameCheck::Mouse(Controller, EKeys::MouseX, FMath::Clamp(Error / 0.25, -40.0, 40.0));
			}

			if (WaitingForLane != INDEX_NONE) {
				// stand, facing across, until a sprint started now would meet no craft
				if (FMath::Abs(Error) < 3.0 && Hero->GetVelocity().Size2D() < 5.0 && LaneIsClear(Arena, ConnectorIndex, WaitingForLane, Target)) {
					WaitingForLane = INDEX_NONE;
					++LanesCrossed;
				}
				else {
					Forward(Controller, false, false);
					return false;
				}
			}
			// walk where the way has jumps, which are sized for a walk; run elsewhere. In the air, keep going.
			const bool bHasJumps = Route.ContainsByPredicate([](const FArenaStep& Each) { return Each.bJump; });
			const bool bInAir = Hero->GetCharacterMovement()->IsFalling();
			Forward(Controller, bInAir || FMath::Abs(Error) < 6.0, !bHasJumps);
			return false;
		}
	};
}
