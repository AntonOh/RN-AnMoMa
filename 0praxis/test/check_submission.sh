#!/bin/bash

set -e

TEST_SCRIPT="$(dirname $0)/test_praxis0.py"

if [[ ! -e ${TEST_SCRIPT} ]]; then
    echo "Requested test file ${TEST_SCRIPT} does not exist!"
    exit 1
fi

echo "Packaging submission..."
PACKAGING_DIR=$(mktemp -d)
cmake -B${PACKAGING_DIR}
cmake --build ${PACKAGING_DIR} -t package_source

echo "Extracting submission..."
SOURCE_DIR=$(mktemp -d)
(cd ${SOURCE_DIR} && find ${PACKAGING_DIR} -name '*.tar.gz' | head -n1 | xargs tar -xzf)
EXTRACTED_SOURCE_DIR=$(find ${SOURCE_DIR} -maxdepth 1 -mindepth 1 -type d | head -n1)

echo "Building submission..."
BUILD_DIR=$(mktemp -d)
cmake -S"${EXTRACTED_SOURCE_DIR}" -B${BUILD_DIR}
cmake --build ${BUILD_DIR} -t hello_world

echo "Executing tests..."
python3 -m pytest -o cache_dir=${BUILD_DIR} ${TEST_SCRIPT}
