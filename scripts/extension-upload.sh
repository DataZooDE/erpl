#!/bin/bash

# Extension upload script

# Usage: ./extension-upload.sh <name> <extension_version> <duckdb_version> <architecture> <s3_bucket> <copy_to_latest> <copy_to_versioned>
# <name>                : Name of the extension
# <extension_version>   : Version (commit / version tag) of the extension
# <duckdb_version>      : Version (commit / version tag) of DuckDB
# <architecture>        : Architecture target of the extension binary
# <s3_bucket>           : S3 bucket to upload to
# <copy_to_latest>      : Set this as the latest version ("true" / "false", default: "false")
# <copy_to_versioned>   : Set this as a versioned version that will prevent its deletion

set -xe

if [[ $4 == wasm* ]]; then
  ext="/tmp/extension/$1.duckdb_extension.wasm"
else
  ext="/tmp/extension/$1.duckdb_extension"
fi

echo $ext

script_dir="$(dirname "$(readlink -f "$0")")"

# compress extension binary
if [[ $4 == wasm_* ]]; then
  brotli < $ext > "$ext.compressed"
else
  #gzip < $ext.append > "$ext.compressed"
  gzip < $ext > "$ext.compressed"
fi

set -e

# Abort if AWS key is not set
if [ -z "$AWS_ACCESS_KEY_ID" ]; then
    echo "No AWS key found, skipping.."
    exit 0
fi

# upload versioned version
if [[ $7 = 'true' ]]; then
  if [[ $4 == wasm* ]]; then
    # Old style paths with additional duckdb-wasm
    aws s3 cp $ext.compressed s3://$5/duckdb-wasm/$1/$2/duckdb-wasm/$3/$4/$1.duckdb_extension.wasm --acl public-read --content-encoding br --content-type="application/wasm"
    # New style paths for duckdb-wasm, more uniforms
    aws s3 cp $ext.compressed s3://$5/$1/$2/$3/$4/$1.duckdb_extension.wasm --acl public-read --content-encoding br --content-type="application/wasm"
  else
    aws s3 cp $ext.compressed s3://$5/$1/$2/$3/$4/$1.duckdb_extension.gz --acl public-read
  fi
fi

# upload to latest version
if [[ $6 = 'true' ]]; then
  if [[ $4 == wasm* ]]; then
    # Old style paths with additional duckdb-wasm
    aws s3 cp $ext.compressed s3://$5/duckdb-wasm/$3/$4/$1.duckdb_extension.wasm --acl public-read --content-encoding br --content-type="application/wasm"
    # New style paths for duckdb-wasm, more uniforms
    aws s3 cp $ext.compressed s3://$5/$3/$4/$1.duckdb_extension.wasm --acl public-read --content-encoding br --content-type="application/wasm"
  else
    aws s3 cp $ext.compressed s3://$5/$3/$4/$1.duckdb_extension.gz --acl public-read
  fi
fi