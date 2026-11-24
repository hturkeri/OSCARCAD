; PersistCAD kurulum betiği — admin izni istemez, kullanıcı profiline kurar.
; Derleme: makensis -DSRC=<dist klasörü> -DVERSION=0.1.0 installer\PersistCAD.nsi
; (GitHub Actions: .github/workflows/kurulum.yml bunu otomatik yapar)

Unicode true
!ifndef SRC
  !define SRC "..\dist"
!endif
!ifndef VERSION
  !define VERSION "0.1.0"
!endif

!define APPNAME   "PersistCAD"
!define PUBLISHER "TEI - Motor Tasarim Araclari Gelistirme"
!define MAINEXE   "persistcad.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} ${VERSION}"
OutFile "PersistCAD_Kurulum_${VERSION}.exe"
RequestExecutionLevel user          ; UAC / admin istemi YOK
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"
SetCompressor /SOLID lzma
SetCompressorDictSize 64
ManifestDPIAware true
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} Kurulum"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "${PUBLISHER}"

!include "MUI2.nsh"
!include "x64.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${MAINEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "${APPNAME} uygulamasını şimdi başlat"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Turkish"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APPNAME} yalnızca 64-bit Windows üzerinde çalışır."
    Abort
  ${EndIf}
  SetShellVarContext current
FunctionEnd

Section "Uygulama" SecMain
  SectionIn RO
  SetShellVarContext current
  ; Önceki sürümün dosyalarını temizle (yalnızca kendi klasörümüz)
  IfFileExists "$INSTDIR\${MAINEXE}" 0 +2
    RMDir /r "$INSTDIR"

  SetOutPath "$INSTDIR"
  File /r "${SRC}\*.*"

  ; Qt eklenti yolunu sabitle (sistemde başka Qt varsa karışmasın)
  FileOpen $0 "$INSTDIR\qt.conf" w
  FileWrite $0 "[Paths]$\r$\nPrefix = .$\r$\nPlugins = .$\r$\n"
  FileClose $0

  ; Kısayollar (kullanıcıya özel)
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${MAINEXE}" "" "$INSTDIR\${MAINEXE}" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Kullanım Kılavuzu.lnk" "$INSTDIR\docs\KULLANIM_KILAVUZU.md"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Örnekler.lnk" "$INSTDIR\samples"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME} Kaldır.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${MAINEXE}" "" "$INSTDIR\${MAINEXE}" 0

  ; .pcad ve .pcapp dosya ilişkilendirmesi (HKCU — admin gerekmez)
  WriteRegStr HKCU "Software\Classes\.pcad" "" "PersistCAD.Model"
  WriteRegStr HKCU "Software\Classes\PersistCAD.Model" "" "PersistCAD Modeli"
  WriteRegStr HKCU "Software\Classes\PersistCAD.Model\DefaultIcon" "" "$INSTDIR\${MAINEXE},0"
  WriteRegStr HKCU "Software\Classes\PersistCAD.Model\shell\open\command" "" '"$INSTDIR\${MAINEXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\.pcapp" "" "PersistCAD.App"
  WriteRegStr HKCU "Software\Classes\PersistCAD.App" "" "PersistCAD Uygulama Paketi"
  WriteRegStr HKCU "Software\Classes\PersistCAD.App\DefaultIcon" "" "$INSTDIR\persistcad_runner.exe,0"
  WriteRegStr HKCU "Software\Classes\PersistCAD.App\shell\open\command" "" '"$INSTDIR\persistcad_runner.exe" "%1"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  ; Kaldırıcı + "Uygulamalar ve özellikler" kaydı (HKCU)
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${MAINEXE}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\${APPNAME}"
  DeleteRegKey HKCU "Software\Classes\PersistCAD.Model"
  DeleteRegKey HKCU "Software\Classes\PersistCAD.App"
  ReadRegStr $0 HKCU "Software\Classes\.pcad" ""
  StrCmp $0 "PersistCAD.Model" 0 +2
    DeleteRegKey HKCU "Software\Classes\.pcad"
  ReadRegStr $0 HKCU "Software\Classes\.pcapp" ""
  StrCmp $0 "PersistCAD.App" 0 +2
    DeleteRegKey HKCU "Software\Classes\.pcapp"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
