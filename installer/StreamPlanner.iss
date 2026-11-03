#define AppName "Stream Planner"
#define AppVersion "1.0.1"
#define AppPublisher "mestisaurio"
#define AppExeName "StreamPlanner.exe"

[Setup]
AppId={{A3D49C16-3B45-4D56-8E19-3C88E7A1B620}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Stream Planner
DefaultGroupName={#AppName}
OutputDir=..\dist
OutputBaseFilename=StreamPlanner-Setup-{#AppVersion}-x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
SetupLogging=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"; Flags: unchecked

[Files]
Source: "..\out\build\x64-Release\StreamPlanner.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Stream Planner"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\Stream Planner"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Abrir Stream Planner"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; Intencionalmente no se borra %LOCALAPPDATA%\StreamPlanner: los datos pertenecen al usuario.
