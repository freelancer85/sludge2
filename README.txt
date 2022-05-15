1. Compile the program
  $ make
gcc -Wall -Werror -pedantic -ggdb -o sludge sludge.c

2. Usage
  # Test with no arguments
  $ ./sludge
Usage: sludge {-lae} {archive.sludge} [files..]

  # Test with invalid archive
  $ ./sludge -l Makefile

  # Test by adding non-existing files
  $ ./sludge -a test.sludge nofile error
stat: No such file or directory
Error: Failed to append file nofile

  #Test to list empty archive
  $ ./sludge -l test.sludge

  #Test to add files
  $ ./sludge -a test.sludge Makefile design.txt
  $ ./sludge -l test.sludge
Size=128        Name=Makefile
Size=837        Name=design.txt

  # Test to extract files (will fail because file exists)
  $ ./sludge -e test.sludge design.txt
open: File exists

  # Test to extract in empty directory, and compare to original file
  $ mkdir temp
  $ cd temp
  $ ../sludge -e ../test.sludge design.txt
  $ diff ../design.txt design.txt
