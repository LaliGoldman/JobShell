all: myshell mypipeline

myshell: myshell.c LineParser.c LineParser.h
	gcc -m32 -Wall myshell.c LineParser.c -o myshell

mypipe: mypipe.c
	gcc -m32 -Wall mypipe.c -o mypipeline
	
valgrind: myshell
	valgrind --leak-check=full --track-origins=yes ./myshell

clean:
	rm -f myshell mypipeline