// Fill out your copyright notice in the Description page of Project Settings.

#include "SenniteSpectator.h"
#include "SenniteCharacter.h"
#include "Engine/Engine.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
ASenniteSpectator::ASenniteSpectator()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	//GetCharacterMovement()->bForceMaxAccel = true;

	BaseTurnRate = 45.f;

	// a spectator flies, and is in nobody's way: players walk and bullets pass through where it is
	GetCharacterMovement()->DefaultLandMovementMode = MOVE_Flying;
	GetCharacterMovement()->BrakingDecelerationFlying = 2048.0f;
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Spectator"));
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);
}

// Called when the game starts or when spawned
void ASenniteSpectator::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) {
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Spectator Begin play"));
	}
}

// Called every frame
void ASenniteSpectator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	//FString LogMsg = FString("spectator tick");
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Spectator tick"));
	//UE_LOG(LogMyGame, Warning, TEXT("%s"), *LogMsg);
}

// Called to bind functionality to input
void ASenniteSpectator::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("MoveForward", this, &ASenniteSpectator::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ASenniteSpectator::MoveRight);
	PlayerInputComponent->BindAxis("MoveUp", this, &ASenniteSpectator::MoveUp);

	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ASenniteSpectator::TurnAtRate);
}

void ASenniteSpectator::MoveForward(float Value) {
	//FString LogMsg = FString("spectator move forward");
	//UE_LOG(LogMyGame, Warning, TEXT("%s"), *LogMsg);
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Move forward ") + FString::SanitizeFloat(Value));
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
		//FVector CurrentLocation = RootComponent->GetComponentLocation();
		//RootComponent->SetWorldLocation(FVector(CurrentLocation.X + Value, CurrentLocation.Y, CurrentLocation.Z));
	}
}

void ASenniteSpectator::MoveRight(float Value) {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Move right ") + FString::SanitizeFloat(Value));
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(Direction, Value);
		//FVector CurrentLocation = RootComponent->GetComponentLocation();
		//RootComponent->SetWorldLocation(FVector(CurrentLocation.X, CurrentLocation.Y + Value, CurrentLocation.Z));
	}
}

void ASenniteSpectator::MoveUp(float Value) {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Move up ") + FString::SanitizeFloat(Value));
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	if (LocalController == nullptr) {
		return;
	}
	bool IsShiftDown = LocalController->IsInputKeyDown(EKeys::LeftShift);

	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Z);
		if (IsShiftDown) {
			AddMovementInput(Direction, -1 * Value);
		}
		else {
			AddMovementInput(Direction, Value);
		}
		/*FVector CurrentLocation = RootComponent->GetComponentLocation();
		if (IsShiftDown) {
			RootComponent->SetWorldLocation(FVector(CurrentLocation.X, CurrentLocation.Y, CurrentLocation.Z + (Value * -1)));
		}*/
	}

}

void ASenniteSpectator::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}


