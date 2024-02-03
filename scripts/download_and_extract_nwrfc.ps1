[CmdletBinding()]
param(
    [string]$SRC = 's3://erpl-resources/sapnwrfc/nwrfc750P_12-70002755_win.zip',
    [string]$TARGET = './nwrfcsdk/win/'
)

function Download-Extract-Move {
    [CmdletBinding()]
    param(
        [string]$s3_url,
        [string]$target_dir
    )

    $tmp_download_dir = "C:\Temp\downloaded"
    $tmp_extract_dir = "C:\Temp\extracted"

    Write-Verbose "Creating directories: $tmp_download_dir, $tmp_extract_dir, $target_dir"
    New-Item -ItemType Directory -Path $tmp_download_dir, $tmp_extract_dir, $target_dir -Force | Out-Null

    Write-Verbose "Downloading file from $s3_url to $tmp_download_dir"
    python -m awscli s3 cp $s3_url $tmp_download_dir

    $zip_file_name = [System.IO.Path]::GetFileName($s3_url)
    Write-Verbose "Zip file name: $zip_file_name"

    Write-Verbose "Extracting $tmp_download_dir\$zip_file_name to $tmp_extract_dir"
    Expand-Archive -Path "$tmp_download_dir\$zip_file_name" -DestinationPath $tmp_extract_dir

    Write-Verbose "Moving files from $tmp_extract_dir\nwrfcsdk to $target_dir"
    Move-Item -Path "$tmp_extract_dir\nwrfcsdk\*" -Destination $target_dir -Force

    Write-Verbose "Removing temporary directories: $tmp_download_dir, $tmp_extract_dir"
    Remove-Item -Path $tmp_download_dir, $tmp_extract_dir -Recurse -Force
}

# Call the function with the Verbose switch to see verbose output
Download-Extract-Move -s3_url $SRC -target_dir $TARGET -Verbose

