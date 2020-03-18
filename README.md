# thread_search
a program that searches a directory tree for files by name. The program receives a directory D and a search term T, and finds every file in D’s directory tree whose name contains T. The program parallelizes its work using threads.

### to compile: gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 -pthread pfind.c

###  Command line arguments:
  - **argv[1]**: search root directory (search for files within this directory and its subdirectories).
  - **argv[2]**: search term (search for file names that include the search term).
  - **argv[3]**: number of searching threads to be used for the search
