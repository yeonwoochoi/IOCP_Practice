# IOCP 에코 서버 테스트 클라이언트 (길이 프리픽스 프레이밍)
#
# 패킷 포맷: [UINT16 PacketSize(헤더포함 전체)][body...]   (리틀엔디안)
#
# 사용 예:
#   .\test_client.ps1                  # "hello" 1패킷 보내고 에코 확인
#   .\test_client.ps1 -Count 50        # 50회 연결/에코/종료 반복
#   .\test_client.ps1 -Merge           # 3패킷을 한 번의 Write로 (서버가 잘라내는지: merge)
#   .\test_client.ps1 -Split           # 1패킷을 1바이트씩 쪼개 전송 (서버가 재조립하는지: split)
#   .\test_client.ps1 -Abrupt -Count 30  # 보내자마자 RST로 끊기 (gen/refcount 경로)

param(
    [int]    $Port   = 11021,
    [string] $Host_  = "127.0.0.1",
    [string] $Msg    = "hello",
    [int]    $Count  = 1,
    [switch] $Merge,
    [switch] $Split,
    [switch] $Abrupt,
    [switch] $Big
)

# 리스트에 [size(2)][body] 추가 (배열 반환 안 함 -> PS 캐스트 함정 회피)
function Add-Packet($list, [string]$body) {
    $bodyBytes = [System.Text.Encoding]::ASCII.GetBytes($body)
    $size = [UInt16]($bodyBytes.Length + 2)
    $list.AddRange([System.BitConverter]::GetBytes($size))
    $list.AddRange($bodyBytes)
}

# body 하나짜리 패킷 바이트 만들기
function Get-PacketBytes([string]$body) {
    $list = New-Object System.Collections.Generic.List[byte]
    Add-Packet $list $body
    return $list.ToArray()
}

function Send-Bytes($stream, $bytes) {
    $b = [byte[]]$bytes
    $stream.Write($b, 0, $b.Length)
}

# 스트림에서 정확히 n바이트 읽기 (Read가 적게 줄 수 있어 루프)
function Read-Exact($stream, [int]$n) {
    $buf = New-Object byte[] $n
    $off = 0
    while ($off -lt $n) {
        $r = $stream.Read($buf, $off, $n - $off)
        if ($r -le 0) { return $null }
        $off += $r
    }
    return ,$buf
}

# 한 패킷 수신: 헤더 2바이트 -> body(size-2)
function Read-Packet($stream) {
    $head = Read-Exact $stream 2
    if ($null -eq $head) { return $null }
    $size = [System.BitConverter]::ToUInt16([byte[]]$head, 0)
    $bodyLen = $size - 2
    $body = Read-Exact $stream $bodyLen
    if ($null -eq $body) { return $null }
    return [System.Text.Encoding]::ASCII.GetString([byte[]]$body, 0, $bodyLen)
}

for ($i = 1; $i -le $Count; $i++) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect($Host_, $Port)
        $stream = $client.GetStream()

        if ($Merge) {
            # 3패킷을 하나의 Write로 붙여 전송 -> 서버가 한 recv에서 3개로 잘라야 함
            $merged = New-Object System.Collections.Generic.List[byte]
            Add-Packet $merged "AAA"
            Add-Packet $merged "BB"
            Add-Packet $merged "CCCC"
            Send-Bytes $stream $merged.ToArray()
            for ($k = 0; $k -lt 3; $k++) {
                Write-Host ("[{0}] merge echo[{1}]='{2}'" -f $i, $k, (Read-Packet $stream))
            }
        }
        elseif ($Split) {
            # 1패킷을 1바이트씩 쪼개 전송 -> 서버가 여러 recv에 걸쳐 재조립해야 함
            $bytes = Get-PacketBytes $Msg
            foreach ($byte in $bytes) {
                $one = New-Object byte[] 1
                $one[0] = $byte
                $stream.Write($one, 0, 1)
                $stream.Flush()
                Start-Sleep -Milliseconds 20
            }
            Write-Host ("[{0}] split echo='{1}'" -f $i, (Read-Packet $stream))
        }
        elseif ($Abrupt) {
            Send-Bytes $stream (Get-PacketBytes $Msg)
            $client.LingerState = New-Object System.Net.Sockets.LingerOption($true, 0)  # RST
            $client.Close()
            Write-Host ("[{0}] sent then abrupt close (RST)" -f $i)
            continue
        }
        elseif ($Big) {
            # 한 연결로 많은 패킷을 보내 링버퍼가 wrap되게 (바디 크기를 바꿔 8192 경계에 패킷이 갈리도록)
            $stream.ReadTimeout = 5000   # 에코 5초 안에 안 오면 예외 -> 진짜 멈춤 감지
            $N = 3000
            $fail = 0
            for ($k = 0; $k -lt $N; $k++) {
                $len  = 1 + ($k % 97)               # 크기 변화로 경계 정렬 회피 -> 패킷이 갈리게 유도
                $body = ("{0}:" -f $k) + ("x" * $len)
                Send-Bytes $stream (Get-PacketBytes $body)
                $echo = Read-Packet $stream
                if ($echo -ne $body) {
                    $fail++
                    Write-Host ("[MISMATCH] k=$k sent='$body' echo='$echo'")
                    if ($fail -ge 5) { break }
                }
                if (($k % 500) -eq 0) { Write-Host ("  ...progress k=$k") }
            }
            Write-Host ("[{0}] big: {1} packets sent/echoed, mismatches={2}" -f $i, $N, $fail)
        }
        else {
            Send-Bytes $stream (Get-PacketBytes $Msg)
            Write-Host ("[{0}] sent='{1}' echo='{2}'" -f $i, $Msg, (Read-Packet $stream))
        }

        $client.Close()
    }
    catch {
        Write-Host ("[{0}] ERROR: {1}" -f $i, $_.Exception.Message)
    }
}
