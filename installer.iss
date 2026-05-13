[Setup]
AppName=CSP Grayscale Viewer
AppVersion=1.0.0
AppPublisher=Antigravity
DefaultDirName={autopf}\CSP-Grayscale-Viewer
DefaultGroupName=CSP Grayscale Viewer

OutputDir=Output
OutputBaseFilename=CSP-Grayscale-Viewer_Setup
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
DisableWelcomePage=no
DisableDirPage=no

[Files]
Source: "deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CSP Grayscale Viewer"; Filename: "{app}\CSP-Grayscale-Viewer.exe"
Name: "{autodesktop}\CSP Grayscale Viewer"; Filename: "{app}\CSP-Grayscale-Viewer.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
