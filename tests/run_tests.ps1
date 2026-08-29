$ErrorActionPreference = 'Stop'

$testDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $testDir
$binary = Join-Path $testDir 'test_elrs_protocol.exe'

& g++ -std=c++17 -Wall -Wextra -Werror `
  (Join-Path $testDir 'test_elrs_protocol.cpp') `
  (Join-Path $projectDir 'ELRSProtocol.cpp') `
  -o $binary

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $binary
exit $LASTEXITCODE

