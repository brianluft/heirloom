# Check for non-UTF-8 text files and convert them.
$extensions = @("*.cpp", "*.h", "*.rc", "*.txt", "*.md", "*.mdc", "*.json", "*.vcxproj", "*.manifest", "*.sh")
$excludePaths = @("*.git*", "*x64*", "*ARM64*")

$files = Get-ChildItem -Path . -Recurse -File -Include $extensions | Where-Object {
    $excluded = $false
    foreach ($pattern in $excludePaths) {
        if ($_.FullName -like $pattern) { $excluded = $true; break }
    }
    -not $excluded
}

$nonUtf8Files = @()
foreach ($file in $files) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false, $true) # throwOnInvalidBytes = true
        $null = $utf8NoBom.GetString($bytes)
    } catch {
        $nonUtf8Files += $file
    }
}

if ($nonUtf8Files.Count -gt 0) {
    Write-Host "Converting the following files to UTF-8 encoding:"
    foreach ($file in $nonUtf8Files) {
        Write-Host "  $($file.FullName)"
        # Read with auto-detected encoding, write back as UTF-8 (no BOM)
        $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::Default)
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($file.FullName, $content, $utf8NoBom)
    }
    Write-Host "Conversion complete."
}
