// The sky a match is played under: day, dusk or night.
//
// Every map is built with one sky: a sun, a sky that fills the shade, the air the sun lights, and a
// haze (Tools/build_arena.py). On an arena that sky is changed to the match's own when the arena is
// built. Which sky a match has comes from the arena's seed, so every player of the match is under the
// same one without being told, and a new match is as likely to be under each as its weight says.
// -Sky=NIGHT on the command line (any sky's name) says which, for a look or a check.
//
// A sky is the sun's height and light, the fill's, how bright the air is, the haze's colour, and
// stars; all in DefaultGame.ini ([/Script/Sennite.SenniteSky]). At night the sun is the moon: the one
// light that casts shadows, low and blue, and the stars are drawn inside the sky's dome. Phone-level
// throughout: nothing here adds a light.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "SenniteSky.generated.h"

class UWorld;

USTRUCT()
struct FSenniteSkyLook
{
	GENERATED_BODY()

	/** What the sky is called: DAY, DUSK, NIGHT. */
	UPROPERTY()
	FName Name;

	/** How likely a match is to be under it, against the others' weights. */
	UPROPERTY()
	float Weight = 1.0f;

	/** The sky the map was built with, left as it is: none of what follows is used. */
	UPROPERTY()
	bool bAsBuilt = false;

	/** How high the sun (at night, the moon) stands above the horizon, in degrees; and its light. */
	UPROPERTY()
	float SunHeight = 48.0f;

	UPROPERTY()
	float SunIntensity = 6.0f;

	UPROPERTY()
	FLinearColor SunColour = FLinearColor::White;

	/** The light that fills the shade, from all round. */
	UPROPERTY()
	float FillIntensity = 1.8f;

	UPROPERTY()
	FLinearColor FillColour = FLinearColor::White;

	/** How bright the air the sun lights is drawn, and the colour it is drawn toward: white leaves it the air's own. */
	UPROPERTY()
	float SkyBrightness = 6.0f;

	UPROPERTY()
	FLinearColor SkyColour = FLinearColor::White;

	/** The colour of the haze over the water and the far land. */
	UPROPERTY()
	FLinearColor Haze = FLinearColor(0.45f, 0.64f, 1.0f);

	/** How bright the stars are; none at 0. */
	UPROPERTY()
	float Stars = 0.0f;
};

UCLASS(Config = Game)
class SENNITE_API USenniteSky : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	TArray<FSenniteSkyLook> Skies;

	/** What the stars are drawn with and on (Tools/build_sky.py; the engine's own sky dome). */
	UPROPERTY(Config)
	FSoftObjectPath StarsMaterial;

	UPROPERTY(Config)
	FSoftObjectPath StarsMesh;

	/** The sky of the match whose arena has this seed: the same on every machine. None if no sky is listed. */
	static const FSenniteSkyLook* For(int32 Seed);

	/** The sky of that name, or none. */
	static const FSenniteSkyLook* Named(FName Name);

	/**
	 * Put this world under its match's sky, once its arena is built. True when there is nothing more to
	 * do: it is done, or the world has no arena and keeps the sky it was built with.
	 */
	static bool SetFor(UWorld* World);

	/** Change the world's sky to this one. False if the world has no sun to change. */
	static bool Put(UWorld* World, const FSenniteSkyLook& Look);

	/** The name of the sky the world was last put under; none while it has the sky it was built with. */
	static FName Now(const UWorld* World);
};
