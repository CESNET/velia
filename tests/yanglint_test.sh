#!/usr/bin/env bash

usage() {
    echo "Usage: $0 [--expected-errors <file>] schema_file... data_file..."
    echo "  --expected-errors <file>    File with expected outputs from yanglint"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --expected-errors)
            shift
            if [[ -z $1 ]]; then
                echo "Error: --expected-errors requires a file argument"
                usage
            fi
            if [[ -n ${ERROR_FILE} ]]; then
                echo "Error: --expected-errors can only be specified once"
                usage
            fi
            ERROR_FILE="$1"
            shift
            ;;
        *)
            YANG_FILES+=("$1")
            shift
            ;;
    esac
done

echo "xx ${ERROR_FILE}"
echo "yy ${YANG_FILES[@]}"
YANGLINT_OUTPUT=$(yanglint -t config -f json ${YANG_FILES[@]} 2>&1)
YANGLINT_EXITCODE=$?

if [[ -n ${ERROR_FILE} ]]; then
    if [[ ${YANGLINT_EXITCODE} -eq 0 ]]; then
        echo "Expected errors but run successfull"
        exit 1
    fi

    DIFF=$(diff <(echo "${YANGLINT_OUTPUT}" | grep -v "Failed to parse input data file") "$ERROR_FILE")
    if [[ $? != 0 ]]; then
        echo "Output does not match expected error file:"
        echo "${DIFF}"
        exit 1
    fi
else
    if [[ ${YANGLINT_EXITCODE} -ne 0 ]]; then
        echo "Expected success but got errors:"
        echo "${YANGLINT_OUTPUT}"
        exit 1
    fi
fi
