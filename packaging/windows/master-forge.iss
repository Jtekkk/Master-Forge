; ===========================================================================
;  Master Forge - Windows installer (Inno Setup 6.3+)
;
;  Installs the 64-bit VST3 into the shared system VST3 folder
;  (C:\Program Files\Common Files\VST3), which every DAW scans.
;
;  Build the plugin first (Release, x64), then compile this script with
;  Inno Setup's command-line compiler:
;
;    iscc /DMyAppVersion=0.3.0 ^
;         "/DMyVST3Dir=..\..\build\MasterForge_artefacts\Release\VST3\Master Forge.vst3" ^
;         master-forge.iss
;
;  The .github/workflows/windows-installer.yml workflow does exactly this on a
;  Windows runner and uploads the resulting installer as an artifact.
; ===========================================================================

#ifndef MyAppVersion
  #define MyAppVersion "0.3.0"
#endif

; Path to the built "Master Forge.vst3" bundle (folder). Override with /DMyVST3Dir=...
#ifndef MyVST3Dir
  #define MyVST3Dir "..\..\build\MasterForge_artefacts\Release\VST3\Master Forge.vst3"
#endif

#define MyAppName     "Master Forge"
#define MyPublisher   "Forge Audio"
#define MyVST3Bundle  "Master Forge.vst3"

[Setup]
; A stable, unique AppId so upgrades/uninstalls are tracked correctly. Keep it fixed.
AppId={{7E5B9C42-3A1D-4F8E-9C2B-0A6D5E4F7B31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyPublisher}
VersionInfoVersion={#MyAppVersion}

; Install straight into the shared VST3 folder; the bundle is created beneath it.
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no

; 64-bit only.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

OutputDir=Output
OutputBaseFilename=MasterForge-{#MyAppVersion}-Windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}
SetupLogging=yes

[Files]
; Copy the whole VST3 bundle into {commoncf64}\VST3\Master Forge.vst3
Source: "{#MyVST3Dir}\*"; DestDir: "{app}\{#MyVST3Bundle}"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\{#MyVST3Bundle}"

[Messages]
WelcomeLabel2=This will install the {#MyAppName} VST3 plugin ({#MyAppVersion}) into your shared VST3 folder.%n%nClose your DAW before continuing, then rescan plugins afterwards.
