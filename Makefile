APP_NAME = tf 

SRC = src

INSTALL_DIR = /usr/bin

CC = gcc
CFLAGS = $(shell pkg-config ncursesw --libs --cflags) -lm

.PHONY: all install uninstall clean

all: $(APP_NAME)

$(APP_NAME): $(SRC)
	$(CC) $(SRC)/main.c $(SRC)/files.c $(SRC)/window.c $(SRC)/app.c $(CFLAGS) -o $(APP_NAME)

install: $(APP_NAME)
	sudo apt-get update
	sudo apt-get install -y libncurses5-dev libncursesw5-dev pkg-config
	sudo cp $(APP_NAME) $(INSTALL_DIR)

uninstall:
	sudo rm -f $(INSTALL_DIR)/$(APP_NAME)

clean:
	rm -f $(APP_NAME)
