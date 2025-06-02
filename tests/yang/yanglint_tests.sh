#!/usr/bin/env bash

while [[ $# -gt 0 ]]; do
    case $1 in
        --output-file)
            shift
            if [[ -z $1 ]]; then
                echo "Error: --error-file requires a file argument"
                usage
            fi
            output_file="$1"
            shift
            ;;
        *)
            yang_files+=("$1")
            shift
            ;;
    esac
done

echo "output_file: $output_file"
echo "yang_files: ${yang_files[@]}"

OUT=$(yanglint -t config -f json ${yang_files[@]} 2>&1)
RES=$?

if [[ -n $output_file ]]; then
    if [[ $RES -ne 1 ]]; then
        echo "Expected error code 1, got $RES"
        exit 1
    fi

    DIFF=$(diff <(echo "$OUT" | grep -v "Failed to parse input data file") "$output_file")
    if [[ $? != 0 ]]; then
        echo "Output does not match expected error file"
        echo "$DIFF"
        exit 1
    fi
else
    if [[ $RES -ne 0 ]]; then
        echo "Expected success, got error code $RES"
        echo "$OUT"
        exit 1
    fi

    echo "Output: $OUT"
fi
