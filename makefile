all:
		gcc -o sender sender.c -lm
		gcc -o receiver receiver.c -lm
