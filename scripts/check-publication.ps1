$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$tracked = @(git -C $repo ls-files)
if ($LASTEXITCODE -ne 0) { throw '无法读取 Git 跟踪文件' }
$docs = @($tracked | Where-Object { $_ -like 'docs/*' })
if ($docs.Count -ne 1 -or $docs[0] -ne 'docs/wwsb.txt') {
    throw "docs 白名单不符：$($docs -join ', ')"
}
if (@($tracked | Where-Object { $_ -like 'build/*' -or $_ -match '(^|/)(bin|obj|debug|out|\.vs)/' }).Count) {
    throw 'Git 跟踪文件含构建或调试产物'
}
if (@($tracked | Where-Object { $_ -match '\.(?:idb|i64|id0|id1|pdb|map|yrrep|partial)$' }).Count) {
    throw 'Git 跟踪文件含数据库、映射或回放产物'
}
$ownText = @($tracked | Where-Object {
    $_ -notlike 'third_party/*' -and $_ -match '\.(?:cpp|h|cs|ps1|md|txt|json|xml|props)$'
})
foreach ($relative in $ownText) {
    $content = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $repo $relative)
    if ($content -match '(?i)C:[/\\]Users[/\\]|CnCNet-Dump|YRHackMod|\.private|full.client') {
        throw "公开文本含本机仓库或私人客户端引用：$relative"
    }
}
Write-Host '公开文件边界检查通过。'
