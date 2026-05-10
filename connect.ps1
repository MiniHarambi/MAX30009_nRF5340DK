# serial_terminal.ps1
# Usage: .\serial_terminal.ps1
# Lists available COM ports, lets you pick one, then connects.

param(
    [int]$BaudRate = 115200
)

# --- Find COM ports ---
$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object

if ($ports.Count -eq 0) {
    Write-Host "No COM ports found. Is the device plugged in?" -ForegroundColor Red
    exit 1
}

# --- Get descriptions from WMI ---
$pnp = Get-WmiObject Win32_PnPEntity -Filter "Caption like '%(COM%'" 2>$null

Write-Host "`nAvailable COM ports:`n" -ForegroundColor Cyan
for ($i = 0; $i -lt $ports.Count; $i++) {
    $desc = ($pnp | Where-Object { $_.Caption -match $ports[$i] }).Caption
    if (-not $desc) { $desc = $ports[$i] }
    Write-Host "  [$i] $desc"
}

# --- Pick a port ---
Write-Host ""
$choice = Read-Host "Select port number (0-$($ports.Count - 1))"

if ($choice -notmatch '^\d+$' -or [int]$choice -ge $ports.Count) {
    Write-Host "Invalid choice." -ForegroundColor Red
    exit 1
}

$PortName = $ports[[int]$choice]

# --- Connect ---
Write-Host "`nOpening $PortName at $BaudRate baud..."
Write-Host "Press Ctrl+C to exit.`n"

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate
$port.ReadTimeout = 100

try {
    $port.Open()
    Write-Host "Connected!`n" -ForegroundColor Green

    while ($true) {
        if ($port.BytesToRead -gt 0) {
            Write-Host -NoNewline $port.ReadExisting()
        }

        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            $port.Write($key.KeyChar.ToString())
        }

        Start-Sleep -Milliseconds 50
    }
}
catch [System.UnauthorizedAccessException] {
    Write-Host "ERROR: Port $PortName is in use by another program." -ForegroundColor Red
    Write-Host "Close VS Code serial terminal or other apps using this port."
}
catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
}
finally {
    if ($port.IsOpen) { $port.Close() }
    Write-Host "`nPort closed."
}