#!/bin/bash

# Exit on the first error
set -e

# Download and extract the NW RFC SDK
BUCKET=${1:-'s3://erpl/sapnwrfc'}
TARGET=${2:-'./nwrfcsdk'}

function download_extract_move() {
  # Parameters
  local s3_url="$1"  # S3 URL of the zip file
  local target_dir="$2"  # Target directory where files should be moved

  local tmp_download_dir="/tmp/downloaded"
  local tmp_extract_dir="/tmp/extracted"

  mkdir -p "$tmp_download_dir" "$tmp_extract_dir" "$target_dir"
  aws s3 cp "$s3_url" "$tmp_download_dir/"

  local zip_file_name=$(basename "$s3_url")

  unzip "$tmp_download_dir/$zip_file_name" -d "$tmp_extract_dir"
  mv "$tmp_extract_dir/nwrfcsdk/"* "$target_dir/"
  rm -r "$tmp_download_dir" "$tmp_extract_dir"
}

download_extract_move "$BUCKET/nwrfc750P_11-70002752_linux.zip" "$TARGET/linux/"
download_extract_move "$BUCKET/nwrfc750P_12-70002755_win.zip" "$TARGET/win/"
download_extract_move "$BUCKET/nwrfc750P_12-80002761_osx.zip" "$TARGET/osx/"
