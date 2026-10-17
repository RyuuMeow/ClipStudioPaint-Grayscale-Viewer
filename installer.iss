[Setup]
AppName=CSP Grayscale Viewer
AppVersion=1.1.0
AppPublisher=Antigravity
DefaultDirName={autopf}\CSP-Grayscale-Viewer
DefaultGroupName=CSP Grayscale Viewer

OutputDir=Output
OutputBaseFilename=CSP-Grayscale-Viewer_Setup
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
MinVersion=10.0
DisableWelcomePage=no
DisableDirPage=no

[Files]
Source: "deploy\CSP-Grayscale-Viewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\CSP-Grayscale-Viewer_Console.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\CSP Grayscale Viewer"; Filename: "{app}\CSP-Grayscale-Viewer.exe"
Name: "{autodesktop}\CSP Grayscale Viewer"; Filename: "{app}\CSP-Grayscale-Viewer.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
