$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NeonTestWindow {
    private delegate bool Callback(IntPtr hwnd, IntPtr param);
    [DllImport("user32.dll")] private static extern bool EnumWindows(Callback cb, IntPtr param);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
    public static int Close(int processId) {
        int closed=0;
        EnumWindows((hwnd, unused) => {
            uint pid; GetWindowThreadProcessId(hwnd, out pid);
            if(pid==(uint)processId) {
                var name=new StringBuilder(256); GetClassName(hwnd,name,256);
                if(name.ToString()=="UnrealWindow" && PostMessage(hwnd,0x10,IntPtr.Zero,IntPtr.Zero)) closed++;
            }
            return true;
        }, IntPtr.Zero);
        return closed;
    }
}
'@
$repo=Split-Path -Parent $PSScriptRoot
$game=Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows/NeonCoast/Binaries/Win64/NeonCoast.exe'
$runId=[guid]::NewGuid().ToString('N')
$started=[DateTime]::UtcNow
$log=Join-Path $repo ('artifacts/ue-release-normal-exit-'+$runId+'.log')
$userDir=Join-Path $repo ('artifacts/normal-exit-user-'+$runId)
New-Item -ItemType Directory -Path $userDir | Out-Null
$reportPath=Join-Path $repo 'artifacts/ue-release-normal-exit-verification.json'
@{status='in_progress';run_id=$runId;started_at=$started.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
$p=Start-Process -FilePath $game -ArgumentList @('-windowed','-ResX=1600','-ResY=900','-nosplash','-unattended',('"-UserDir='+$userDir+'"'),('"-abslog='+$log+'"')) -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -PassThru
try {
    $ready=$false
    for($n=0;$n -lt 40;$n++) {
        Start-Sleep -Seconds 1
        if((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -SimpleMatch 'NC_FRONTEND_READY page=title' -Quiet)){$ready=$true;break}
        $p.Refresh();if($p.HasExited){throw 'Game exited before ready'}
    }
    if(-not $ready){throw 'Game was not ready in 40 seconds'}
    Start-Sleep -Seconds 10
    $windowsClosed=[NeonTestWindow]::Close($p.Id)
    if($windowsClosed -lt 1){throw 'No Unreal game window found for this test process'}
    if(-not $p.WaitForExit(60000)){throw 'Game did not exit'}
    $p.Refresh()
    if($p.ExitCode -ne 0){throw "Normal game exit failed: $($p.ExitCode)"}
    if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:' -Quiet){throw 'Engine failure logged'}
    [ordered]@{
        status='passed';run_id=$runId;executable=$game
        executable_sha256=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
        content_files=@(Get-ChildItem -LiteralPath (Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows/NeonCoast/Content/Paks') -File | Sort-Object Name | ForEach-Object {
            [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
        user_dir=$userDir;checked_at=(Get-Date -Format o)
        mode='Packaged DX12, normal game, no CSV; isolated UserDir; WM_CLOSE to its own UnrealWindow after title menu ready and 10 seconds'
        exit_code=$p.ExitCode;engine_failures=0;title_menu_ready=$ready;windows_closed=$windowsClosed;log=$log
    }|ConvertTo-Json -Depth 4|Tee-Object -FilePath $reportPath
} finally {
    $p.Refresh();if(-not $p.HasExited){Stop-Process -Id $p.Id}
}

