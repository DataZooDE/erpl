#!/bin/bash

if [ $# -eq 0 ]
    then
        directory="."
else
        directory=$1
fi

pushd . > /dev/null
cd $directory

for xmlfile in *.xml
do
    echo "Formatting $xmlfile"
    xmllint --format "$xmlfile" > "$xmlfile.bak"
    mv "$xmlfile.bak" "$xmlfile"
done

popd > /dev/null
