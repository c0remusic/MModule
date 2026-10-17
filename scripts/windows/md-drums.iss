; MoDule's Windows installer (Inno Setup 6): MoDule.vst3 where every VST3 host looks, its README and licences
; beside the uninstaller. It wraps the release's zip as it is, so the plug-in inside is the one the gate tested; built
; by .github/workflows/md-drums-windows-installer.yml:
;   ISCC "/DVersion=X.Y.Z" "/DStage=<the zip's folder, unpacked>" /O<output> scripts\windows\md-drums.iss
; Uninstalling leaves the user's data alone (Documents\c0remusic\MoDule: the Bank, the flash image). Installing
; removes what MD Drums' installer (0.1.1, the same AppId) put: the same plug-in under its old name.

#ifndef Version
  #error Give the version: /DVersion=X.Y.Z
#endif
#ifndef Stage
  #error Give the unpacked release zip's folder: /DStage=<folder>
#endif

[Setup]
AppId={{C6C3930C-077F-42D7-97A3-A3A587304143}
AppName=MoDule
AppVersion={#Version}
AppVerName=MoDule {#Version} (preview)
AppPublisher=c0remusic
AppPublisherURL=https://c0remusic.github.io/MModule/
AppSupportURL=https://github.com/c0remusic/MModule/issues
AppUpdatesURL=https://github.com/c0remusic/MModule/releases
DefaultDirName={commonpf64}\c0remusic\MoDule
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=MoDule-{#Version}-preview-Windows-x64-setup
InfoAfterFile={#Stage}\README.txt
UninstallDisplayName=MoDule
VersionInfoVersion={#Version}

[InstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\MD Drums.vst3"
Type: filesandordirs; Name: "{commonpf64}\c0remusic\MD Drums"

[Files]
Source: "{#Stage}\MoDule.vst3\*"; DestDir: "{commoncf64}\VST3\MoDule.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\NOTICE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\LICENSE-AGPL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Inter-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion
