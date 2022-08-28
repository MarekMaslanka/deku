# Author: Marek Maślanka
# Project: KernelHotReload
# URL: https://github.com/MarekMaslanka/KernelHotReload

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
	./kernel_hot_reload.sh $(WORKDIR) deploy

build:
	./kernel_hot_reload.sh $(WORKDIR) build

sync:
	./kernel_hot_reload.sh $(WORKDIR) sync