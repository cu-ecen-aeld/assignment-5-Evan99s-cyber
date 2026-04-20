#!/bin/sh

if [ $# -ne 2 ]; then
	echo "Error: L1"
	exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]; then
        echo "Error: L2"
        exit 1
fi

num_files=$(find "$filesdir" -type f | wc -l)

num_matches=$(grep -R "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are $num_files and the number of matching lines are $num_matches"
