if [ -d .venv ]; then
	source .venv/bin/activate
else
	python -m venv .venv
	source .venv/bin/activate
	pip install -r scripts/requirements.txt
	pip install pymetis #only works via pip on linux
fi

"$@"