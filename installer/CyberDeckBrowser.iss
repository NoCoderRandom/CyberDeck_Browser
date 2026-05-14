#define MyAppName "CyberDeck Browser"
#ifndef MyAppVersion
#define MyAppVersion "0.1.0"
#endif
#define MyAppPublisher "CyberDeck Browser"
#define MyAppExeName "CyberDeckBrowser.exe"

#ifndef SourceDir
#define SourceDir "..\dist\installer-staging\app"
#endif

#ifndef OutputDir
#define OutputDir "..\dist"
#endif

[Setup]
AppId={{D64D1C0E-56D8-4878-979D-A95F6E2E7B6E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\CyberDeck Browser
DefaultGroupName=CyberDeck Browser
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=CyberDeckBrowserSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#MyAppName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CyberDeck Browser"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall CyberDeck Browser"; Filename: "{uninstallexe}"
Name: "{autodesktop}\CyberDeck Browser"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch CyberDeck Browser"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
