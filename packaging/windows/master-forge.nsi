; ===========================================================================
;  Master Forge - NSIS installer
;
;  NSIS (makensis) runs on Linux, so this is used to package the MinGW
;  cross-compiled Windows VST3 into an .exe installer without a Windows
;  machine. (The CI / MSVC path uses master-forge.iss / Inno Setup instead.)
;
;    makensis -DVERSION=0.3.0 -DBUNDLE_PARENT=/abs/.../Release/VST3
;             -DOUTFILE=/abs/MasterForge-0.3.0-Windows-x64-setup.exe
;             packaging/windows/master-forge.nsi
; ===========================================================================

Unicode true

!define APPNAME    "Master Forge"
!define PUBLISHER  "Forge Audio"
!define BUNDLENAME "Master Forge.vst3"
!define UNINSTKEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\MasterForge"

!ifndef VERSION
  !define VERSION "0.3.0"
!endif
!ifndef OUTFILE
  !define OUTFILE "MasterForge-${VERSION}-Windows-x64-setup.exe"
!endif
!ifndef BUNDLE_PARENT
  !error "Pass -DBUNDLE_PARENT=<folder that contains 'Master Forge.vst3'>"
!endif

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel admin
InstallDir "$COMMONFILES64\VST3"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey  "ProductName"   "${APPNAME}"
VIAddVersionKey  "FileVersion"   "${VERSION}"
VIAddVersionKey  "CompanyName"   "${PUBLISHER}"
VIAddVersionKey  "FileDescription" "${APPNAME} VST3 installer"
VIAddVersionKey  "LegalCopyright" "${PUBLISHER}"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Master Forge VST3" SecVST3
    SectionIn RO
    SetRegView 64
    SetOutPath "$INSTDIR"
    ; Recursively copy the whole VST3 bundle into the shared VST3 folder.
    File /r "${BUNDLE_PARENT}/${BUNDLENAME}"

    WriteUninstaller "$INSTDIR\${BUNDLENAME}\Uninstall.exe"

    WriteRegStr   HKLM "${UNINSTKEY}" "DisplayName"     "${APPNAME}"
    WriteRegStr   HKLM "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   HKLM "${UNINSTKEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\${BUNDLENAME}\Uninstall.exe"'
    WriteRegStr   HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR\${BUNDLENAME}"
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    SetRegView 64
    RMDir /r "$INSTDIR\${BUNDLENAME}"
    DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
