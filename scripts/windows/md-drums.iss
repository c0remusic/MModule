; MD Drums' Windows installer (Inno Setup 6): MD Drums.vst3 where every VST3 host looks, its README and licences
; beside the uninstaller. It wraps the release's zip as it is, so the plug-in inside is the one the gate tested; built
; by .github/workflows/md-drums-windows-installer.yml:
;   ISCC "/DVersion=X.Y.Z" "/DStage=<the zip's folder, unpacked>" /O<output> scripts\windows\md-drums.iss
; Uninstalling leaves the user's data alone (Documents\c0remusic\MD Drums: the Bank, the flash image).

#ifndef Version
  #error Give the version: /DVersion=X.Y.Z
#endif
#ifndef Stage
  #error Give the unpacked release zip's folder: /DStage=<folder>
#endif

[Setup]
AppId={{C6C3930C-077F-42D7-97A3-A3A587304143}
AppName=MD Drums
AppVersion={#Version}
AppVerName=MD Drums {#Version} (preview)
AppPublisher=c0remusic
AppPublisherURL=https://c0remusic.github.io/MModule/
AppSupportURL=https://github.com/c0remusic/MModule/issues
AppUpdatesURL=https://github.com/c0remusic/MModule/releases
DefaultDirName={commonpf64}\c0remusic\MD Drums
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=MD-Drums-{#Version}-preview-Windows-x64-setup
InfoAfterFile={#Stage}\README.txt
UninstallDisplayName=MD Drums
VersionInfoVersion={#Version}

[Files]
Source: "{#Stage}\MD Drums.vst3\*"; DestDir: "{commoncf64}\VST3\MD Drums.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\NOTICE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\LICENSE-AGPL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Inter-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion
