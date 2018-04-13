#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#define abs(x) ((x) < 0 ? -(x) : (x))
#define N 5
#define K 1

typedef struct Coord {
	int row;
	int col;
} Coord;

typedef struct Position { // Describes a position; 0 = white, 1 = black
	Coord knights[2][K];
	Coord kings[2];
	int checks[2]; // Number of checks delivered
	int number_of_knights[2]; // Number of knights remaining
	int turn;
	int in_check;
	Coord checking_square;
} Position;

typedef struct Move {
	Coord start;
	Coord end;
} Move;

Move possible_moves[N * 5];

int knight_attacks(Coord *knight_position, Coord *coord) {
	int row_difference = abs(knight_position->row - coord->row);
	int col_difference = abs(knight_position->col - coord->col);
	return (row_difference == 2 && col_difference == 1) || (row_difference == 1 && col_difference == 2);
}

int king_attacks(Coord *king_position, Coord *coord) {
	int row_difference = abs(king_position->row - coord->row);
	int col_difference = abs(king_position->col - coord->col);
	return row_difference <= 1 && col_difference <= 1;
}

int is_protected(Position *pp, Coord *coord) { // Check to see whether checking piece is defended by another piece
	if (king_attacks(&(pp->kings[!pp->turn]), coord)) return 1;
	for (int i = 0; i < pp->number_of_knights[!pp->turn]; i++) {
		if (knight_attacks(&(pp->knights[!pp->turn][i]), coord)) return 1;
	}
	return 0;
}

int is_occupied(Position *pp, Coord *coord) {
	if (coord->row == pp->kings[pp->turn].row && coord->col == pp->kings[pp->turn].col) return 1;
	for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
		if (coord->row == pp->knights[pp->turn][i].row && coord->col == pp->knights[pp->turn][i].col) return 1;
	}
	return 0;
}

int get_knight_moves(Position *pp, Coord *coord, Coord *move_array) {
	int i = 0;
	Coord possible_moves[8] = { {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1} };
	for (int j = 0; j < 8; j++) {
		int new_row = coord->row - possible_moves[j].row;
		int new_col = coord->col - possible_moves[j].col;
		Coord new_coord = { new_row, new_col };
		if (0 <= new_row && new_row < N && 0 <= new_col && new_col < N && !is_occupied(pp, &new_coord)) {
			move_array[i].row = new_row;
			move_array[i].col = new_col;
			i++;
		}
	}
	return i;
}

int get_king_moves(Position *pp, Coord *move_array) {
	int i = 0;
	Coord possible_moves[8] = { {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1} };
	for (int j = 0; j < 8; j++) {
		int new_row = pp->kings[pp->turn].row - possible_moves[j].row;
		int new_col = pp->kings[pp->turn].col - possible_moves[j].col;
		Coord new_coord = { new_row, new_col };
		if (0 <= new_row && new_row < N && 0 <= new_col && new_col < N && !is_occupied(pp, &new_coord) && !is_protected(pp, &new_coord)) {
			move_array[i].row = new_row;
			move_array[i].col = new_col;
			i++;
		}
	}
	return i;
}

// Note: "pp" refers to a pointer to a Position structure.

Move *get_moves(Position *pp, Move *move_ptr) { // Returns pointer to end of array
	if (pp->in_check) {
		// Determine whether checking knight can be captured
		for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
			if (knight_attacks(&(pp->knights[pp->turn][i]), &(pp->checking_square))) {
				move_ptr->start = pp->knights[pp->turn][i];
				move_ptr->end = pp->checking_square;
				move_ptr++;
			}
		}
		return move_ptr;
	}
	// Not in check
	Coord move_array[8];
	for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
		int number_of_possible_moves = get_knight_moves(pp, &(pp->knights[pp->turn][i]), move_array);
		for (int j = 0; j < number_of_possible_moves; j++) {
			move_ptr->start = pp->knights[pp->turn][i];
			move_ptr->end = move_array[j];
			move_ptr++;
		}
	}
	int number_of_possible_moves = get_king_moves(pp, move_array);
	for (int j = 0; j < number_of_possible_moves; j++) {
		move_ptr->start = pp->kings[pp->turn];
		move_ptr->end = move_array[j];
		move_ptr++;
	}
	return move_ptr;
}

int main(int argc, char **argv) {
	Coord c1 = {1, 1};
	Coord c2 = {0, 0};
	Coord knights[2][K] = { {{1, 1}}, {{3, 3}} };
	Coord kings[2] = { {1, 1}, {4, 4} };
	Position position = {
		.knights = {{{0, 0}}, {{3, 2}}},
		.kings = {{1, 1}, {3, 1}},
		.checks = {1, 1},
		.number_of_knights = {1, 1},
		.turn = 1,
		.in_check = 0,
		.checking_square = {1, 1}
	};
	Move move_array[40];
	Move *ptr = get_moves(&position, move_array);
	Move *p = move_array;
	while (p < ptr) {
		printf("%d %d %d %d\n", p->start.row, p->start.col, p->end.row, p->end.col);
		p++;
	}
	printf("%ld\n", ptr-move_array);
	return 0;
}