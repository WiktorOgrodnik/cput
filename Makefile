CC = gcc
SRC_DIR = src
INC_DIR = .
OBJ_DIR = obj
CFLAGS = -std=gnu17 -pthread -g -Wall -Wextra -Wpedantic -O2
NAME = cput
OBJS = $(addprefix $(OBJ_DIR)/, main.o ring_buffer.o)

all: pre $(OBJS)
	@$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

pre:
	@if [ ! -d $(OBJ_DIR) ]; then mkdir $(OBJ_DIR); fi;

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(CC) $(CFLAGS) -c -I$(INC_DIR) $< -o $@ $(GTK)

clean:
	@if [ -d $(OBJ_DIR) ]; then rm -r -f $(OBJ_DIR); fi;

distclean: clean
	@rm -f $(NAME)
