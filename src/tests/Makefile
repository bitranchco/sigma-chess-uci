# Makefile for Chess Engine Tests

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -I.. -I"../Data Structures" -I../Misc -I../Evaluation -I"../Move Generation" -I../Searching -include ../General.h

# Engine source files needed for tests (with quoted paths for spaces)
ENGINE_SRCS = \
	../Data\ Structures/Board.c \
	../Data\ Structures/Attack.c \
	../Data\ Structures/Move.c \
	../Move\ Generation/PerformMove.c \
	../Move\ Generation/MoveGen.c \
	../Misc/Time.c \
	../Misc/HashCode.c \
	../Searching/SearchMisc.c \
	../Searching/Threats.c \
	../Searching/Selection.c \
	../Searching/TransTables.c \
	../Searching/NodeSearch.c \
	../Evaluation/Evaluate.c \
	../Evaluation/PieceVal.c

# Test source files
TEST_SRCS = \
	test_main.c \
	test_helpers.c \
	test_attack.c \
	test_pawn_moves.c \
	test_movegen.c \
	test_noncaptures.c \
	test_perform_move.c \
	test_check_evasion.c \
	test_searchmisc.c \
	test_nodesearch.c \
	test_selection.c \
	test_transtables.c \
	test_evaluate.c \
	Engine_stub.c

# Object files (in current directory to avoid space issues)
ENGINE_OBJS = Board.o Attack.o Move.o PerformMove.o MoveGen.o Time.o HashCode.o SearchMisc.o Threats.o Selection.o TransTables.o NodeSearch.o Evaluate.o PieceVal.o
TEST_OBJS = test_main.o test_helpers.o test_attack.o test_pawn_moves.o test_movegen.o test_noncaptures.o test_perform_move.o test_check_evasion.o test_searchmisc.o test_nodesearch.o test_selection.o test_transtables.o test_evaluate.o Engine_stub.o

# Output
TEST_BIN = test_engine

# Default target
all: $(TEST_BIN)

$(TEST_BIN): $(ENGINE_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Explicit rules for engine source files
Board.o: ../Data\ Structures/Board.c
	$(CC) $(CFLAGS) -c "$<" -o $@

Attack.o: ../Data\ Structures/Attack.c
	$(CC) $(CFLAGS) -c "$<" -o $@

Move.o: ../Data\ Structures/Move.c
	$(CC) $(CFLAGS) -c "$<" -o $@

PerformMove.o: ../Move\ Generation/PerformMove.c
	$(CC) $(CFLAGS) -c "$<" -o $@

MoveGen.o: ../Move\ Generation/MoveGen.c
	$(CC) $(CFLAGS) -c "$<" -o $@

Time.o: ../Misc/Time.c
	$(CC) $(CFLAGS) -c "$<" -o $@

HashCode.o: ../Misc/HashCode.c
	$(CC) $(CFLAGS) -c "$<" -o $@

SearchMisc.o: ../Searching/SearchMisc.c
	$(CC) $(CFLAGS) -c "$<" -o $@

Threats.o: ../Searching/Threats.c
	$(CC) $(CFLAGS) -c "$<" -o $@

Selection.o: ../Searching/Selection.c
	$(CC) $(CFLAGS) -c "$<" -o $@

TransTables.o: ../Searching/TransTables.c
	$(CC) $(CFLAGS) -c "$<" -o $@

NodeSearch.o: ../Searching/NodeSearch.c
	$(CC) $(CFLAGS) -c "$<" -o $@

Evaluate.o: ../Evaluation/Evaluate.c
	$(CC) $(CFLAGS) -c "$<" -o $@

PieceVal.o: ../Evaluation/PieceVal.c
	$(CC) $(CFLAGS) -c "$<" -o $@

# Pattern rule for test files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run tests
test: $(TEST_BIN)
	./$(TEST_BIN)

# Clean
clean:
	rm -f $(ENGINE_OBJS) $(TEST_OBJS) $(TEST_BIN) test_selection Engine_stub.o

.PHONY: all test clean
