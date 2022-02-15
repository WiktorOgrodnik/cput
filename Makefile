CC = gcc
SRC_DIR = src
INC_DIR = .
OBJ_DIR = obj
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2
NAME = cut
OBJS = $(addprefix $(OBJ_DIR)/, main.o)

all: $(OBJS)
	@$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(CC) $(CFLAGS) -c -I$(INC_DIR) $< -o $@ $(GTK)

clean:
	@rm -f $(NAME) $(OBJS)