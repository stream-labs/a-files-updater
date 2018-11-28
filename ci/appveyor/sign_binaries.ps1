Copy-Item `
        -Path "build\slobs-updater.exe" `
        -Destination "build\slobs-updater-signed-$env:BUILD_TYPE.exe"

& secure-file\tools\secure-file `
        -decrypt ci\streamlabsp12.pfx.enc `
        -secret "$env:STREAMLABS_PFX_SECRET" `
        -out ci\streamlabsp12.pfx

if ($LASTEXITCODE -ne 0) {
    exit 1
}

& "$env:SIGN_TOOL" `
        sign /as /p "$env:STREAMLABS_SECRET" `
        /f ci\streamlabsp12.pfx `
        /d "Streamlabs OBS Updater" `
        "build\slobs-updater-signed-$env:BUILD_TYPE.exe"

if ($LASTEXITCODE -ne 0) {
    exit 1
}

Push-AppveyorArtifact "build\slobs-updater-signed-$env:BUILD_TYPE.exe"