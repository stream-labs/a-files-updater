Copy-Item `
        -Path "$env:BUILD_DIR\$env:BUILD_TYPE\slobs-updater.exe" `
        -Destination "$env:BUILD_DIR\$env:BUILD_TYPE\slobs-updater-signed-$env:BUILD_TYPE.exe"

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
        "$env:BUILD_DIR\$env:BUILD_TYPE\slobs-updater-signed-$env:BUILD_TYPE.exe"

if ($LASTEXITCODE -ne 0) {
    exit 1
}

Push-AppveyorArtifact "$env:BUILD_DIR\$env:BUILD_TYPE\slobs-updater-signed-$env:BUILD_TYPE.exe"