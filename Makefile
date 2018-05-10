programm := Multi

source_files = $(wildcard *.c) # все исходные файлы в текущей директории
obj_files = $(patsubst %.c,%.o,$(source_files)) 
.PHONY: clean libs
all: $(programm)

$(programm): $(obj_files)
	gcc -o $@ $^ -lpthread

$(obj_files): %.o : %.c
	gcc -c $<


clean:
	rm -rf $(programm) *.o *.a *.so

