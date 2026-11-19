// The sky a match is played under: see SenniteSky.h.

#include "SenniteSky.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SenniteArena.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenniteSky, Log, All);

namespace
{
	/** What marks the sun of a world whose sky was changed, and the stars' own dome. */
	const TCHAR* SkyTag = TEXT("Sky:");
	const FName StarsTag(TEXT("SkyStars"));

	ADirectionalLight* SunOf(const UWorld* World)
	{
		TActorIterator<ADirectionalLight> It(World);
		return It ? *It : nullptr;
	}

	/** The dome the sky is drawn on: a phone draws the sky's air only on a mesh whose material is marked as sky. */
	AStaticMeshActor* DomeOf(const UWorld* World)
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It) {
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			const UMaterialInterface* Face = Mesh != nullptr && Mesh->GetStaticMesh() != nullptr ? Mesh->GetMaterial(0) : nullptr;
			if (Face != nullptr && Face->GetMaterial() != nullptr && Face->GetMaterial()->bIsSky) {
				return *It;
			}
		}
		return nullptr;
	}
}

const FSenniteSkyLook* USenniteSky::For(int32 Seed)
{
	const TArray<FSenniteSkyLook>& Skies = GetDefault<USenniteSky>()->Skies;
	float Total = 0.0f;
	for (const FSenniteSkyLook& Each : Skies) {
		Total += FMath::Max(0.0f, Each.Weight);
	}
	if (Skies.Num() == 0 || Total <= 0.0f) {
		return nullptr;
	}
	// a draw of its own from the seed: the land's draws are not the sky's
	FRandomStream Draw(Seed ^ 0x5eed5c1e);
	Draw.FRand();
	float At = Draw.FRand() * Total;
	for (const FSenniteSkyLook& Each : Skies) {
		At -= FMath::Max(0.0f, Each.Weight);
		if (At < 0.0f) {
			return &Each;
		}
	}
	return &Skies.Last();
}

const FSenniteSkyLook* USenniteSky::Named(FName Name)
{
	return GetDefault<USenniteSky>()->Skies.FindByPredicate([Name](const FSenniteSkyLook& Each) { return Each.Name == Name; });
}

FName USenniteSky::Now(const UWorld* World)
{
	if (const ADirectionalLight* Sun = SunOf(World)) {
		for (const FName& Tag : Sun->Tags) {
			const FString Said = Tag.ToString();
			if (Said.StartsWith(SkyTag)) {
				return FName(*Said.Mid(FCString::Strlen(SkyTag)));
			}
		}
	}
	return NAME_None;
}

bool USenniteSky::SetFor(UWorld* World)
{
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer) {
		return true;
	}
	TActorIterator<ASenniteArena> Arena(World);
	if (!Arena) {
		return true; // no arena: the sky the map was built with
	}
	if (!Arena->IsBuilt()) {
		return false;
	}
	const FSenniteSkyLook* Look = For(Arena->GetPlan().Seed);
	FString Asked;
	if (FParse::Value(FCommandLine::Get(), TEXT("Sky="), Asked) && !Asked.IsEmpty()) {
		if (const FSenniteSkyLook* Said = Named(FName(*Asked))) {
			Look = Said;
		}
		else {
			UE_LOG(LogSenniteSky, Warning, TEXT("-Sky=%s is not a sky of [/Script/Sennite.SenniteSky]"), *Asked);
		}
	}
	if (Look != nullptr) {
		Put(World, *Look);
	}
	return true;
}

bool USenniteSky::Put(UWorld* World, const FSenniteSkyLook& Look)
{
	ADirectionalLight* Sun = SunOf(World);
	if (Sun == nullptr) {
		return false;
	}
	Sun->Tags.RemoveAll([](const FName& Tag) { return Tag.ToString().StartsWith(SkyTag); });
	Sun->Tags.Add(FName(*(FString(SkyTag) + Look.Name.ToString())));
	UE_LOG(LogSenniteSky, Display, TEXT("The sky is %s"), *Look.Name.ToString());
	if (Look.bAsBuilt) {
		return true;
	}
	// the sun, or the moon: from the side the map's sun is on, at this sky's height
	if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent())) {
		Sun->SetActorRotation(FRotator(-Look.SunHeight, Sun->GetActorRotation().Yaw, 0.0));
		Light->SetIntensity(Look.SunIntensity);
		Light->SetLightColor(Look.SunColour);
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It) {
		if (USkyLightComponent* Fill = It->GetLightComponent()) {
			Fill->SetIntensity(Look.FillIntensity);
			Fill->SetLightColor(Look.FillColour);
		}
	}
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It) {
		if (USkyAtmosphereComponent* Air = It->GetComponent()) {
			Air->SetSkyLuminanceFactor(Look.SkyColour * Look.SkyBrightness);
		}
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) {
		if (UExponentialHeightFogComponent* Fog = It->GetComponent()) {
			Fog->SetFogInscatteringColor(Look.Haze);
		}
	}
	// the stars: on a dome of their own just inside the sky's, so the land hides them and the sky does not
	AStaticMeshActor* Stars = nullptr;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It) {
		if (It->ActorHasTag(StarsTag)) {
			Stars = *It;
			break;
		}
	}
	if (Look.Stars <= 0.0f) {
		if (Stars != nullptr) {
			Stars->Destroy();
		}
		return true;
	}
	const USenniteSky* Settings = GetDefault<USenniteSky>();
	UStaticMesh* Shape = Cast<UStaticMesh>(Settings->StarsMesh.TryLoad());
	UMaterialInterface* Face = Cast<UMaterialInterface>(Settings->StarsMaterial.TryLoad());
	const AStaticMeshActor* Dome = DomeOf(World);
	if (Shape == nullptr || Face == nullptr || Dome == nullptr) {
		UE_LOG(LogSenniteSky, Warning, TEXT("The stars' mesh, material or the sky's dome is missing: this sky has no stars"));
		return true;
	}
	if (Stars == nullptr) {
		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags |= RF_Transient;
		Stars = World->SpawnActor<AStaticMeshActor>(Dome->GetActorLocation(), Dome->GetActorRotation(), Spawn);
		if (Stars == nullptr) {
			return true;
		}
		Stars->SetReplicates(false);
		Stars->Tags.Add(StarsTag);
		UStaticMeshComponent* Mesh = Stars->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Shape);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Stars->SetActorScale3D(Dome->GetActorScale3D() * 0.96);
	}
	if (UMaterialInstanceDynamic* Lit = Stars->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0, Face)) {
		Lit->SetScalarParameterValue(TEXT("Brightness"), Look.Stars);
	}
	return true;
}
