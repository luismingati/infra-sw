all: main.c
	gcc main.c -o shell -pthread
clean:
	rm shell