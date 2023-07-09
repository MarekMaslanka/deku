# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

.PHONY: deploy

all: mklivepatch elfutils

WORKDIR=
ifdef workdir
	WORKDIR=-w $(workdir)
endif

CC ?= gcc
CFLAG ?= -Werror -Wall -Wpedantic -Wextra -Wno-gnu-zero-variadic-macro-arguments

ELFUTILS_FLAGS= $(CFLAG) -lelf -lopcodes

mklivepatch: mklivepatch.c
	$(CC) mklivepatch.c $(CFLAG) -lelf -o $@

elfutils: elfutils.c
	$(shell echo "void t() { init_disassemble_info(NULL, 0, NULL); }" | \
			$(CC) -DPACKAGE=1 -include dis-asm.h -S -o - -x c - > /dev/null 2>&1)
	$(CC) elfutils.c $(ELFUTILS_FLAGS) -DDISASSEMBLY_STYLE_SUPPORT=$(.SHELLSTATUS) -o $@

clean:
	rm -f mklivepatch elfutils

deploy:
	$(warning Using DEKU with "make deploy" is deprecated and will be removed soon. Instead, use the "./deku deploy" command.)
	./deku $(WORKDIR) deploy

build:
	$(warning Using DEKU with "make build" is deprecated and will be removed soon. Instead, use the "./deku build" command.)
	./deku $(WORKDIR) build

sync:
	$(warning Using DEKU with "make sync" is deprecated and will be removed soon. Instead, use the "./deku sync" command.)
	./deku $(WORKDIR) sync
