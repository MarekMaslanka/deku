# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

.PHONY: deploy

all: mklivepatch elfutils

WORKDIR=
ifdef workdir
	WORKDIR=-w $(workdir)
endif

mklivepatch: mklivepatch.c
	gcc mklivepatch.c -lelf -o mklivepatch

elfutils: elfutils.c
	gcc elfutils.c -g -lelf -lopcodes -o elfutils

clean:
	rm -f mklivepatch elfutils

deploy:
	./deku $(WORKDIR) deploy

build:
	./deku $(WORKDIR) build

sync:
	./deku $(WORKDIR) sync
