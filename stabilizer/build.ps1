Param(
    [string] $NdkDir
)

$ErrorActionPreference = 'Stop'

function Get-NdkDir {
    param([string] $Explicit)
    if ($Explicit) { return $Explicit }
    $root = (Get-Item $PSScriptRoot).Parent.FullName
    $localProps = Join-Path $root 'local.properties'
    if (-not (Test-Path $localProps)) { throw "local.properties not found at $localProps" }
    $ndkLine = Select-String -Path $localProps -Pattern '^ndk.dir=(.*)$'
    if (-not $ndkLine) { throw "ndk.dir not set in $localProps" }
    return $ndkLine.Matches[0].Groups[1].Value
}

$ndk = Get-NdkDir -Explicit $NdkDir
$tool = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin'
$sysroot = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\sysroot'
$clang = Join-Path $tool 'clang.exe'

$srcDir = Join-Path $PSScriptRoot 'zygisk'
$src = Join-Path $srcDir 'stabilizer.c'

$targets = @(
    @{ abi = 'armeabi-v7a'; triple = 'armv7a-linux-androideabi21' },
    @{ abi = 'arm64-v8a';    triple = 'aarch64-linux-android21' }
)

Push-Location $srcDir
try {
    foreach ($t in $targets) {
        $out = Join-Path $srcDir "$($t.abi).so"
        & $clang --target=$($t.triple) --sysroot="$sysroot" -fPIC -shared -O2 $src -o $out
        Write-Host "Built $out" -ForegroundColor Green
    }

    Pop-Location
    Push-Location $PSScriptRoot

    $buildDir = Join-Path $PSScriptRoot 'build'
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }
    $zipPath = Join-Path $buildDir 'zygisk_stabilizer.zip'

    # Include module files and zygisk directory
    $paths = @('module.prop','post-fs-data.sh','service.sh','zygisk')
    Compress-Archive -Path $paths -DestinationPath $zipPath -Force
    Write-Host "Packaged $zipPath" -ForegroundColor Cyan
}
finally {
    Pop-Location
}
