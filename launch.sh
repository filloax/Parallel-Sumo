#!/bin/bash

mkdir -p data

if [ -d .venv ]; then
	source .venv/bin/activate
else
	python -m venv .venv
	source .venv/bin/activate
	pip install -r scripts/requirements.txt
fi

./main "$@"
