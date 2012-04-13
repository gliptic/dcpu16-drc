clang -c asm.c vm.c parser.c && gcc -o vm asm.o vm.o parser.o -ludis86 && ./vm
