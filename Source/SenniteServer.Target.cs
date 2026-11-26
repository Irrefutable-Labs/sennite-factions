using UnrealBuildTool;
using System.Collections.Generic;

// The publisher's match server as a dedicated server (no rendering, no player of its own), for Linux on the
// games host. Built only from a source-built engine (the launcher's engine builds no Server target):
// Tools/build_linux_match_server.sh.
public class SenniteServerTarget : TargetRules
{
	public SenniteServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("Sennite");
	}
}
