using UnrealBuildTool;

public class Sennite : ModuleRules
{
	public Sennite(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG", "ProceduralMeshComponent", "AnimationCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "ApplicationCore", "HTTP", "Json", "Sockets", "Niagara" });

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// the game opens the DSM Wallet app on the phone when it waits on it: Java added to the game's activity
			PrivateDependencyModuleNames.Add("Launch");
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(ModuleDirectory, "Sennite_UPL.xml"));
		}
	}
}
