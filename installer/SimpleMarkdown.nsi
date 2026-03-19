; SimpleMarkdown NSIS Installer Script

!include "MUI2.nsh"
!include "FileFunc.nsh"

; ============== Basic Info ==============
!define APP_NAME "SimpleMarkdown"
!
!define APP_PUBLISHER "SimpleMarkdown"
!define APP_EXE "SimpleMarkdown.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define FILETYPE_KEY "SimpleMarkdown.Document"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${APP_NAME}-${APP_VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES\${APP_NAME}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel admin
Unicode True

; ============== MUI Settings ==============
!define MUI_ABORTWARNING
!define MUI_ICON "..\resources\app-icon.ico"
!define MUI_UNICON "..\resources\app-icon.ico"

; ============== Pages ==============
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; ============== Languages ==============
!insertmacro MUI_LANGUAGE "SimpChinese"

; ============== Installer Sections ==============
Section "!$(^Name)" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"
    File "dist\${APP_EXE}"
    File "dist\*.dll"

    SetOutPath "$INSTDIR\platforms"
    File "dist\platforms\*.dll"

    IfFileExists "dist\imageformats\*.dll" 0 +3
    SetOutPath "$INSTDIR\imageformats"
    File /nonfatal "dist\imageformats\*.dll"

    IfFileExists "dist\styles\*.dll" 0 +3
    SetOutPath "$INSTDIR\styles"
    File /nonfatal "dist\styles\*.dll"

    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1

    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $0

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}" "" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKCR "Applications\${APP_EXE}" "FriendlyAppName" "${APP_NAME}"
    WriteRegStr HKCR "Applications\${APP_EXE}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

    WriteRegStr HKCR "${FILETYPE_KEY}" "" "Markdown Document"
    WriteRegStr HKCR "${FILETYPE_KEY}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKCR "${FILETYPE_KEY}\shell" "" "open"
    WriteRegStr HKCR "${FILETYPE_KEY}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

    !macro RegisterExtension EXT
        WriteRegStr HKCR ".${EXT}\OpenWithProgids" "${FILETYPE_KEY}" ""
    !macroend

    !insertmacro RegisterExtension "md"
    !insertmacro RegisterExtension "markdown"
    !insertmacro RegisterExtension "txt"

    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'
SectionEnd

Section "Start Menu" SecStartMenu
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

; ============== Section Descriptions ==============
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Install SimpleMarkdown and all required files"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create Start Menu shortcuts"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create Desktop shortcut"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ============== Uninstaller ==============
Section "Uninstall"
    !macro UnregisterExtension EXT
        DeleteRegValue HKCR ".${EXT}\OpenWithProgids" "${FILETYPE_KEY}"
    !macroend

    !insertmacro UnregisterExtension "md"
    !insertmacro UnregisterExtension "markdown"
    !insertmacro UnregisterExtension "txt"

    DeleteRegKey HKCR "${FILETYPE_KEY}"
    DeleteRegKey HKCR "Applications\${APP_EXE}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"

    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\Uninstall.exe"

    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\styles"
    RMDir "$INSTDIR"

    Delete "$DESKTOP\${APP_NAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APP_NAME}"

    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'
SectionEnd
