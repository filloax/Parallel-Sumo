#!/bin/bash

mkdir -p data

if [ -d .venv ]; then
	python -m venv .venv
fi
source .venv/bin/activate
pip install -r scripts/requirements.txt

./main "$@"
