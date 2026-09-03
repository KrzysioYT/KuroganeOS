[CmdletBinding()]
param(
    [int]$BasePort = 18080
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$endpointJob = {
    param([int]$Port, [string]$Mode)
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Any, $Port)
    $listener.Start()
    try {
        while ($true) {
            $client = $listener.AcceptTcpClient()
            try {
                if ($Mode -eq 'echo') {
                    $stream = $client.GetStream()
                    $request = New-Object byte[] 13
                    $read = 0
                    while ($read -lt $request.Length) {
                        $count = $stream.Read($request, $read, $request.Length - $read)
                        if ($count -le 0) { break }
                        $read += $count
                    }
                    $response = [System.Text.Encoding]::ASCII.GetBytes('KURO-TCP-PONG')
                    $stream.Write($response, 0, $response.Length)
                    $stream.Flush()
                }
                elseif ($Mode -eq 'timeout') {
                    Start-Sleep -Seconds 120
                }
                elseif ($Mode -eq 'close') {
                    Start-Sleep -Seconds 2
                }
                elseif ($Mode -eq 'reset') {
                    $client.LingerState = [System.Net.Sockets.LingerOption]::new($true, 0)
                }
            }
            finally {
                $client.Close()
            }
        }
    }
    finally {
        $listener.Stop()
    }
}

$refusedPort = $BasePort + 1
$timeoutPort = $BasePort + 2
$resetPort = $BasePort + 3
$closePort = $BasePort + 4
$jobs = @(
    Start-Job -ScriptBlock $endpointJob -ArgumentList @($BasePort, 'echo')
    Start-Job -ScriptBlock $endpointJob -ArgumentList @($closePort, 'close')
    Start-Job -ScriptBlock $endpointJob -ArgumentList @($timeoutPort, 'timeout')
    Start-Job -ScriptBlock $endpointJob -ArgumentList @($refusedPort, 'reset')
    Start-Job -ScriptBlock $endpointJob -ArgumentList @($resetPort, 'reset')
)
Write-Host "[qemu-tcp] echo=$BasePort refused=$refusedPort timeout=$timeoutPort reset=$resetPort close=$closePort"
Write-Host "[qemu-tcp] refused port $refusedPort sends a deterministic TCP RST"
try {
    while ($true) {
        foreach ($job in $jobs) {
            if ($job.State -eq 'Failed') {
                Receive-Job -Job $job
                throw "TCP endpoint job failed on port index $($jobs.IndexOf($job))."
            }
        }
        Start-Sleep -Seconds 1
    }
}
finally {
    $jobs | Stop-Job -ErrorAction SilentlyContinue
    $jobs | Remove-Job -Force -ErrorAction SilentlyContinue
}
