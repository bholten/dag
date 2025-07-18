.PHONY: tests

build:
	gcc dagwood.c -o dagwood -ltcl

bear:
	bear -- gcc lib/klib/*.h src/dagwood.c src/core/*.c src/core/*.h src/tcl/*.h src/tcl/*.c -o build/dagwood -ltcl

debug:
	gcc -g -O0 lib/klib/*.h src/dagwood.c src/core/*.c src/core/*.h src/tcl/*.h src/tcl/*.c -o build/dagwood -ltcl

tests:
	gcc tests/tests_tt.c lib/klib/*.h src/core/*.c src/core/*.h src/tcl/*.h src/tcl/*.c -o build/tests_tt -ltcl
