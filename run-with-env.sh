if [ -d .venv ]; then
	source .venv/bin/activate
	if [ $? -ne 0 ]; then exit; fi
else
	python -m venv .venv
	if [ $? -ne 0 ]; then exit; fi
	source .venv/bin/activate
	if [ $? -ne 0 ]; then exit; fi
	pip install -r scripts/requirements.txt
	if [ $? -ne 0 ]; then exit; fi
	pip install pymetis #only works via pip on linux
	if [ $? -ne 0 ]; then exit; fi
fi

"$@"