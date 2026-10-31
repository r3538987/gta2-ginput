$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
New-Item -ItemType Directory -Path deps -Force | Out-Null
$dependencies = @(
    @{
        Name = 'SDL3-devel-3.4.18-VC.zip'
        Uri = 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip'
        Hash = '78D84602AE616CFE26B33A73B7D3B9B6B56E8A6650E53D92C9C6CA938F747ACD'
    },
    @{
        Name = 'misi-source.zip'
        Uri = 'https://gtamp.com/forum/download/file.php?id=1891'
        Hash = 'FFD40B4CE2DAB94EB4DB1E02E8114DE30A3015A0379AA95AED64F6BD57D85621'
    }
)
foreach ($dependency in $dependencies) {
    $path = Join-Path 'deps' $dependency.Name
    if (-not (Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -Uri $dependency.Uri -OutFile $path -UseBasicParsing
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $dependency.Hash) {
        throw "Unexpected checksum for $path"
    }
}
Write-Output 'Dependency archives verified. Run build.ps1 next.'
