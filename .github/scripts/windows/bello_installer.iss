; Inno Setup script for Bello
[Setup]
AppName=Bello
AppVersion=1.0.0
DefaultDirName={pf64}\Bello
Compression=lzma
OutputBaseFilename=bello-1.0.0-windows-x64
PrivilegesRequired=admin
DisableProgramGroupPage=yes

[Files]
; The installer script will have the staging path injected by the packaging script
Source: "{#SourcePath}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Bello"; Filename: "{app}\bello.exe"

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello"; ValueType: string; ValueName: "DisplayName"; ValueData: "Bello"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\duckdb"
