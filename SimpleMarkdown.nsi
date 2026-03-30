; SimpleMarkdown NSIS Installer Script

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

; ============== Basic Info ==============
!define APP_NAME "SimpleMarkdown"
!define APP_VERSION "0.1.0"
!define APP_PUBLISHER "SimpleMarkdown"
!define APP_EXE "SimpleMarkdown.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define FILETYPE_KEY "SimpleMarkdown.Document"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${APP_NAME}-${APP_VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel admin
Unicode True

; ============== MUI Settings ==============
!define MUI_ABORTWARNING
!define MUI_ICON "..\resources\app-icon.ico"
!define MUI_UNICON "..\resources\app-icon.ico"

; ============== Variables ==============
Var WasRunning

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
Function .onInit
    ; 检测进程是否在运行
    StrCpy $WasRunning "0"
    nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${APP_EXE}" /NH'
    Pop $0 ; return code
    Pop $1 ; output
    StrCmp $0 "error" done_check
    ${If} $1 != ""
        ; 检查输出是否包含进程名
        Push $1
        Push "${APP_EXE}"
        Call StrContains
        Pop $0
        ${If} $0 != ""
            StrCpy $WasRunning "1"
            nsExec::Exec 'taskkill /F /IM ${APP_EXE}'
            Sleep 500
        ${EndIf}
    ${EndIf}
    done_check:
FunctionEnd

; 字符串包含检测函数
Function StrContains
    Exch $R1 ; search string
    Exch
    Exch $R2 ; source string
    Push $R3
    Push $R4
    StrLen $R3 $R1
    StrCpy $R4 ""
    loop:
        StrCpy $R4 $R2 $R3
        StrCmp $R4 $R1 found
        StrLen $R4 $R2
        IntCmp $R4 $R3 notfound notfound
        StrCpy $R2 $R2 "" 1
        Goto loop
    found:
        StrCpy $R1 $R4
        Goto done
    notfound:
        StrCpy $R1 ""
    done:
    Pop $R4
    Pop $R3
    Pop $R2
    Exch $R1
FunctionEnd

Section "!$(^Name)" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"
    File "dist\${APP_EXE}"
    File "dist\*.dll"
    File "..\CHANGELOG.md"

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

    ; 如果安装前进程在运行，安装完成后自动重启
    ${If} $WasRunning == "1"
        Exec '"$INSTDIR\${APP_EXE}"'
    ${EndIf}
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
    Delete "$INSTDIR\CHANGELOG.md"
    Delete "$INSTDIR\Uninstall.exe"

    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\styles"
    RMDir "$INSTDIR"

    Delete "$DESKTOP\${APP_NAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APP_NAME}"

    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'
SectionEnd
