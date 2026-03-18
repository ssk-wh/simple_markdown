; EasyMarkdown NSIS Installer Script

!include "MUI2.nsh"
!include "FileFunc.nsh"

; ============== Basic Info ==============
!define APP_NAME "EasyMarkdown"
!define APP_VERSION "0.1.0"
!define APP_PUBLISHER "EasyMarkdown"
!define APP_EXE "EasyMarkdown.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define FILETYPE_KEY "EasyMarkdown.Document"

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
Section "主程序 (必需)" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"

    ; Main executable
    File "dist\${APP_EXE}"

    ; DLLs
    File "dist\*.dll"

    ; Qt plugins - platforms
    SetOutPath "$INSTDIR\platforms"
    File "dist\platforms\*.dll"

    ; Qt plugins - imageformats
    IfFileExists "dist\imageformats\*.dll" 0 +2
    SetOutPath "$INSTDIR\imageformats"
    File /nonfatal "dist\imageformats\*.dll"

    ; Qt plugins - styles
    IfFileExists "dist\styles\*.dll" 0 +2
    SetOutPath "$INSTDIR\styles"
    File /nonfatal "dist\styles\*.dll"

    ; Create uninstaller
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Write registry info for Add/Remove Programs
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1

    ; Calculate installed size
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $0

    ; Register application path
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}" "" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKCR "Applications\${APP_EXE}" "FriendlyAppName" "${APP_NAME}"
    WriteRegStr HKCR "Applications\${APP_EXE}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

    ; Register file type class
    WriteRegStr HKCR "${FILETYPE_KEY}" "" "Markdown Document"
    WriteRegStr HKCR "${FILETYPE_KEY}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKCR "${FILETYPE_KEY}\shell" "" "open"
    WriteRegStr HKCR "${FILETYPE_KEY}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

    ; Register OpenWithProgids for markdown extensions
    !macro RegisterExtension EXT
        WriteRegStr HKCR ".${EXT}\OpenWithProgids" "${FILETYPE_KEY}" ""
    !macroend

    !insertmacro RegisterExtension "md"
    !insertmacro RegisterExtension "markdown"
    !insertmacro RegisterExtension "txt"

    ; Notify shell of association changes
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'
SectionEnd

Section "开始菜单快捷方式" SecStartMenu
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "桌面快捷方式" SecDesktop
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

; ============== Section Descriptions ==============
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "安装 EasyMarkdown 主程序及所有必需文件"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "在开始菜单创建快捷方式"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "在桌面创建快捷方式"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ============== Uninstaller ==============
Section "Uninstall"
    ; Remove file type registration
    !macro UnregisterExtension EXT
        DeleteRegValue HKCR ".${EXT}\OpenWithProgids" "${FILETYPE_KEY}"
    !macroend

    !insertmacro UnregisterExtension "md"
    !insertmacro UnregisterExtension "markdown"
    !insertmacro UnregisterExtension "txt"

    DeleteRegKey HKCR "${FILETYPE_KEY}"
    DeleteRegKey HKCR "Applications\${APP_EXE}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}"

    ; Remove uninstall registry key
    DeleteRegKey HKLM "${UNINSTALL_KEY}"

    ; Remove files
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\Uninstall.exe"

    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\styles"
    RMDir "$INSTDIR"

    ; Remove shortcuts
    Delete "$DESKTOP\${APP_NAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APP_NAME}"

    ; Notify shell of association changes
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'
SectionEnd
