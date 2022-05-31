all: codeutils mklivepatch

codeutils: codeutils.c
	gcc codeutils.c -o codeutils

mklivepatch: mklivepatch.c
	gcc mklivepatch.c -lelf -o mklivepatch

clean:
	rm -f codeutils mklivepatch

deploy:
	./kernel_hot_reload.sh deploy

build:
	./kernel_hot_reload.sh build

sync:
	./kernel_hot_reload.sh sync