#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#define abs(x) ((x) < 0 ? -(x) : (x))
#define N 6
#define K (N-2)
#define WHITE 0
#define BLACK 1
#define HASH_DEPTH 5
#define NOT_IN_HASH 101 // Must be larger than greatest possible evaluation
#define ALPHA_REJECT -102
#define BETA_REJECT 102


typedef struct Coord {
	int8_t row;
	int8_t col;
} Coord;

typedef struct Position { // Describes a position; 0 = white, 1 = black
	Coord knights[2][N-1];
	Coord kings[2];
	int8_t checks[2]; // Number of checks delivered
	int8_t number_of_knights[2]; // Number of knights remaining
	uint8_t turn;
	uint8_t in_check;
	Coord checking_square;
} Position;

typedef struct Move {
	Coord start;
	Coord end;
} Move;

typedef struct Evaluated_Move {
	Move move;
	int evaluation;
} Evaluated_Move;

typedef struct Evaluated_Position {
	uint64_t compressed_position;
	int evaluation;
	uint8_t visits;
	uint8_t depth;
} Evaluated_Position;

void print_move(Move *move);
int judge_position(Position *pp, Evaluated_Move *mp, int depth);
int best_move_of_white(Position *pp, Move *mp, int alpha, int beta, int depth);
int best_move_of_black(Position *pp, Move *mp, int alpha, int beta, int depth);
void make_move(Position *pp_old, Position *pp_new, Move *move);
int check_prime(int p, int *prime_array, int n);
void print_position(Position *pp);

int positions_evaluated = 0;
Evaluated_Position *hash_table;
int hash_table_size;
int hash_check = 0;
int hash_hit = 0;

uint64_t compress_position(Position *pp) { // Associates each position with a unique 64-bit integer
	uint64_t cmp_white = 0, cmp_black = 0, result = 0;
	for (int i = 0; i < pp->number_of_knights[WHITE]; i++) {
		uint64_t square = N * pp->knights[WHITE][i].row + pp->knights[WHITE][i].col;
		cmp_white = cmp_white | (square << (i * 6)); // 2^6 = 64
	}
	uint64_t square = N * pp->kings[WHITE].row + pp->kings[WHITE].col;
	cmp_white = cmp_white | (square << K * 6);
	for (int i = 0; i < pp->number_of_knights[BLACK]; i++) {
		uint64_t square = N * pp->knights[BLACK][i].row + pp->knights[BLACK][i].col;
		cmp_black = cmp_black | (square << (i * 6));
	}
	square = N * pp->kings[BLACK].row + pp->kings[BLACK].col;
	cmp_black = cmp_black | (square << (K * 6));
	uint64_t prefix = (pp->turn) | (pp->checks[WHITE] << 1) | (pp->checks[BLACK] << 2);
	return prefix | (cmp_white << 3) | (cmp_black << 33);
}

int hash(uint64_t position) {
	return position % (hash_table_size - 1);
}

int check_hash(Position *pp, int depth) {
	hash_check++;
	uint64_t compressed_position = compress_position(pp);
	int p_hash = hash(compressed_position);
	for (int i = p_hash; i < p_hash + HASH_DEPTH; i++) {
		if (hash_table[p_hash].compressed_position == compressed_position && hash_table[p_hash].depth >= depth) {
			hash_hit++;
			//printf("Hash hit: %llu %llu\n", hash_table[p_hash].compressed_position, compressed_position);
			//print_position(pp);
			//printf("Evaluation: %d\n", hash_table[p_hash].evaluation);
			hash_table[p_hash].visits++;
			return hash_table[p_hash].evaluation;
		}
	}
	return NOT_IN_HASH;
}

void add_to_hash(Position *pp, int evaluation, int depth) {
	uint64_t compressed_position = compress_position(pp);
	int p_hash = hash(compressed_position);
	int worst_index = p_hash;
	int worst_score = hash_table[p_hash].visits + pow(5, hash_table[p_hash].depth);
	for (int i = p_hash; i < p_hash + HASH_DEPTH; i++) {
		if (hash_table[p_hash].compressed_position == 0) { // Entry empty; can add forthwith
			hash_table[worst_index] = (Evaluated_Position){compressed_position, evaluation, 0, depth};
		}
		int p_score = hash_table[i].visits + pow(5, hash_table[i].depth);
		if (p_score < worst_score) {
			worst_score = p_score;
			worst_index = i % hash_table_size;
		}
	}
	if (worst_score <= pow(5, depth)) hash_table[worst_index] = (Evaluated_Position){compressed_position, evaluation, 0, depth};
}

double score(Evaluated_Position *pp) { // Return the usefulness of a position in the hash table
	return pp->visits + pow(5, pp->depth);
}

int find_max_index(Evaluated_Move array[], int length) { // Length must be greater than zero
	int max_index = 0;
	int max = array[0].evaluation;
	for (int i = 0; i < length; i++) {
		if (array[i].evaluation > max) {
			max = array[i].evaluation;
			max_index = i;
		}
	}
	return max_index;
}

int find_min_index(Evaluated_Move array[], int length) { // Length must be greater than zero
	int min_index = 0;
	int min = array[0].evaluation;
	for (int i = 0; i < length; i++) {
		if (array[i].evaluation < min) {
			min = array[i].evaluation;
			min_index = i;
		}
	}
	return min_index;
}

void print_move(Move *move) {
	printf("(%d, %d) to (%d, %d)\n", move->start.row, move->start.col, move->end.row, move->end.col);
}

void print_position(Position *pp) {
	int board[N][N];
	memset(board, 0, sizeof(board));
	board[pp->kings[0].row][pp->kings[0].col] = 9812;
	board[pp->kings[1].row][pp->kings[1].col] = 9818;
	for (int i = 0; i < pp->number_of_knights[0]; i++) {
		board[pp->knights[0][i].row][pp->knights[0][i].col] = 9816;
	}
	for (int i = 0; i < pp->number_of_knights[1]; i++) {
		board[pp->knights[1][i].row][pp->knights[1][i].col] = 9822;
	}
	for (int i = 0; i < N; i++) {
		printf("|");
		for (int j = 0; j < N; j++) {
			if (board[i][j] != 0) {
				printf("%lc|", board[i][j]);
			}
			else {
				printf(" |");
			}
		}
		printf("\n");
	}
}

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
	if (king_attacks(&(pp->kings[1 - pp->turn]), coord)) return 1;
	for (int i = 0; i < pp->number_of_knights[1 - pp->turn]; i++) {
		if (knight_attacks(&(pp->knights[1 - pp->turn][i]), coord)) return 1;
	}
	return 0;
}

int occupied_by(Position *pp, Coord *coord) {
	if (coord->row == pp->kings[pp->turn].row && coord->col == pp->kings[pp->turn].col) return K;
	for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
		if (coord->row == pp->knights[pp->turn][i].row && coord->col == pp->knights[pp->turn][i].col) return i;
	}
	return -1;
}

int get_knight_moves(Position *pp, Coord *coord, Coord *move_array) {
	int i = 0;
	Coord possible_moves[8] = { {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1} };
	for (int j = 0; j < 8; j++) {
		int new_row = coord->row - possible_moves[j].row;
		int new_col = coord->col - possible_moves[j].col;
		Coord new_coord = { new_row, new_col };
		if (0 <= new_row && new_row < N && 0 <= new_col && new_col < N && occupied_by(pp, &new_coord) == -1) {
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
		Coord new_coord = {new_row, new_col};
		if (0 <= new_row && new_row < N && 0 <= new_col && new_col < N && occupied_by(pp, &new_coord) == -1 && !is_protected(pp, &new_coord)) {
			if (move_array != NULL) {
				move_array[i].row = new_row;
				move_array[i].col = new_col;
			}
			i++;
		}
	}
	return i;
}

int get_moves(Position *pp, Evaluated_Move *move_ptr) { // Adds moves to array refrenced by move_ptr and returns number of moves added
	Evaluated_Move *start_ptr = move_ptr;
	Coord move_array[8];
	if (pp->in_check) {
		// Determine whether checking knight can be captured by knight
		for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
			if (knight_attacks(&(pp->knights[pp->turn][i]), &(pp->checking_square))) {
				move_ptr->move.start = pp->knights[pp->turn][i];
				move_ptr->move.end = pp->checking_square;
				move_ptr++;
			}
		}
	}
	else {
		for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
			int number_of_possible_moves = get_knight_moves(pp, &(pp->knights[pp->turn][i]), move_array);
			for (int j = 0; j < number_of_possible_moves; j++) {
				move_ptr->move.start = pp->knights[pp->turn][i];
				move_ptr->move.end = move_array[j];
				move_ptr++;
			}
		}
	}
	int number_of_possible_moves = get_king_moves(pp, move_array);
	for (int j = 0; j < number_of_possible_moves; j++) {
		move_ptr->move.start = pp->kings[pp->turn];
		move_ptr->move.end = move_array[j];
		move_ptr++;
	}
	return move_ptr - start_ptr;
}

int move_knight(Position *pp_new, Move *move) {
	for (int i = 0; i < pp_new->number_of_knights[1 - pp_new->turn]; i++) {
		if (move->start.row == pp_new->knights[1 - pp_new->turn][i].row && move->start.col == pp_new->knights[1 - pp_new->turn][i].col) {
			pp_new->knights[1 - pp_new->turn][i].row = move->end.row;
			pp_new->knights[1 - pp_new->turn][i].col = move->end.col;
			return knight_attacks(&(move->end), &(pp_new->kings[pp_new->turn]));
		}
	}
	return -1;
}

void make_move(Position *pp_old, Position *pp_new, Move *move) {
	*pp_new = *pp_old;
	pp_new->turn = 1 - pp_old->turn;
	// Remove knight occupying destination square, if any
	int occupier = occupied_by(pp_new, &(move->end));
	if (occupier != -1) {
		for (int i = occupier; i < pp_new->number_of_knights[pp_new->turn] - 1; i++) {
			pp_new->knights[pp_new->turn][i] = pp_new->knights[pp_new->turn][i+1];
		}
		pp_new->number_of_knights[pp_new->turn]--;
	}
	// Move piece from source square to destination square
	if (move->start.row == pp_old->kings[pp_old->turn].row && move->start.col == pp_old->kings[pp_old->turn].col) {
		memcpy(&(pp_new->kings[pp_old->turn]), &(move->end), sizeof(move->end));
		pp_new->in_check = 0;
	}
	else {
		pp_new->in_check = move_knight(pp_new, move);
		pp_new->checking_square = move->end;
		pp_new->checks[pp_new->turn] += pp_new->in_check;
	}
}

int game_over(Position *pp) {
	if (pp->checks[0] == 3 || pp->checks[1] == 3) return 1;
	if (pp->in_check == 0) return 0;
	// Test for checkmate
	return get_king_moves(pp, NULL) == 0;
}

int evaluate_position(Position *pp) {
	//print_position(pp);
	//printf("%d %d %d %d\n", pp->number_of_knights[0], pp->checks[0], pp->number_of_knights[1], pp->checks[1]);
	return (1 * pp->number_of_knights[0] - 1 * pp->checks[0]) - (1 * pp->number_of_knights[1] - 1 * pp->checks[1]);
}

Evaluated_Move get_best_move(Position *pp, int depth) {
	Evaluated_Move best_move;
	add_to_hash(pp, 0, depth); // If neither player can do better than a repetition of position occurs, the position is even
	if (pp->turn == WHITE) {
		best_move.evaluation = best_move_of_white(pp, &best_move.move, ALPHA_REJECT, BETA_REJECT, depth);
	}
	else {
		best_move.evaluation = best_move_of_black(pp, &best_move.move, ALPHA_REJECT, BETA_REJECT, depth);
	}
	return best_move;
}

void evaluate_all(Position *pp, int depth) {
	Evaluated_Move best_move;
	Evaluated_Move em_array[8 * N];
	Move best_responses[8 * N];
	Position position_after_move;
	int n = get_moves(pp, em_array); // Number of moves
	for (int i = 0; i < n; i++) { // Evaluate each possible move
		make_move(pp, &position_after_move, &em_array[i].move);
		best_move = get_best_move(&position_after_move, depth - 1);
		printf("Evaluation: %d ", best_move.evaluation);
		print_move(&em_array[i].move);
	}
}

void print_em(Evaluated_Move em) {
	printf("Evaluation: %d\tMove: ", em.evaluation);
	print_move(&em.move);
}

void print_moves(Evaluated_Move *array, int n) {
	for (int i = 0; i < n; i++) {
		printf("Evaluation: %d\tMove: ", array[i].evaluation);
		print_move(&array[i].move);
	}
}

int best_move_of_white(Position *pp, Move *mp, int alpha, int beta, int depth) { // Returns the evaluation of White's best move from the position "*pp"
	if (depth == 0) return evaluate_position(pp);
	Evaluated_Move em_array[8 * N];
	Position position_after_move;
	int n = get_moves(pp, em_array); // Number of moves
	for (int i = 0; i < n; i++) { // Evaluate each possible move
		make_move(pp, &position_after_move, &em_array[i].move);
		int evaluation = check_hash(&position_after_move, depth);
		if (evaluation != NOT_IN_HASH) {
			em_array[i].evaluation = evaluation;
		}
		else {
			em_array[i].evaluation = best_move_of_black(&position_after_move, mp, alpha, beta, depth - 1);
			if (em_array[i].evaluation != ALPHA_REJECT && em_array[i].evaluation != BETA_REJECT) {
				add_to_hash(&position_after_move, em_array[i].evaluation, depth);
			}
		}
		if (em_array[i].evaluation >= beta) return BETA_REJECT; // Black should avoid this branch
		alpha = em_array[i].evaluation > alpha ? em_array[i].evaluation : alpha; 
	}
	int best_index = find_max_index(em_array, n);
	*mp = em_array[best_index].move;
	return em_array[best_index].evaluation;
}

int best_move_of_black(Position *pp, Move *mp, int alpha, int beta, int depth) { // Returns the evaluation of Black's best move from the position "*pp"
	if (depth == 0) return evaluate_position(pp);
	Evaluated_Move em_array[8 * N];
	Position position_after_move;
	int n = get_moves(pp, em_array); // Number of moves
	for (int i = 0; i < n; i++) { // Evaluate each possible move
		make_move(pp, &position_after_move, &em_array[i].move);
		int evaluation = check_hash(&position_after_move, depth);
		if (evaluation != NOT_IN_HASH) {
			em_array[i].evaluation = evaluation;
		}
		else {
			em_array[i].evaluation = best_move_of_white(&position_after_move, mp, alpha, beta, depth - 1);
			if (em_array[i].evaluation != ALPHA_REJECT && em_array[i].evaluation != BETA_REJECT) {
				add_to_hash(&position_after_move, em_array[i].evaluation, depth);
			}
		}
		if (em_array[i].evaluation <= alpha) return ALPHA_REJECT; // White should avoid this branch
		beta = em_array[i].evaluation < beta ? em_array[i].evaluation : beta; 
	}
	int best_index = find_min_index(em_array, n);
	*mp = em_array[best_index].move;
	return em_array[best_index].evaluation;
}

int judge_position(Position *pp, Evaluated_Move *mp, int depth) {
	positions_evaluated++;
	if (depth == 0) return evaluate_position(pp);
	Evaluated_Move em_array[8 * N];
	int n = get_moves(pp, em_array); // Number of moves
	for (int i = 0; i < n; i++) {
		Position position_after_move;
		make_move(pp, &position_after_move, &em_array[i].move);
		em_array[i].evaluation = judge_position(&position_after_move, em_array + i, depth - 1);
	}
	int best_index = (pp->turn == WHITE) ? find_max_index(em_array, n) : find_min_index(em_array, n);
	return em_array[best_index].evaluation;
}

void get_starting_position(Position *pp) {
	int j = 0;
	for (int i = 2; i < N; i++) {
		pp->knights[WHITE][j] = (Coord){N-1, i};
		pp->knights[BLACK][j] = (Coord){0, N-i-1};
		j++;
	}
	pp->kings[WHITE] = (Coord){N-1, 0};
	pp->kings[BLACK] = (Coord){0, N-1};
	pp->checks[WHITE] = 3;
	pp->checks[BLACK] = 3;
	pp->number_of_knights[WHITE] = N-2;
	pp->number_of_knights[BLACK] = N-2;
	pp->turn = WHITE;
	pp->in_check = 0;
	pp->checking_square = (Coord){0, 0};
}

Move get_user_move(void) {
	char buf[20] = {'\0'};
	read(fileno(stdin), buf, 20);
	int c1, r1, c2, r2;
	c1 = buf[0] - 'a';
	r1 = buf[1] - '0';
	c2 = buf[2] - 'a';
	r2 = buf[3] - '0';
	Move move = {{N - r1, c1}, {N - r2, c2}};
	return move;
}

int get_prime(int n) {
	int prime_array[n];
	int prime_index = 0;
	for (int i = 2; i < n; i++) {
		if (check_prime(i, prime_array, prime_index)) prime_array[prime_index++] = i;
	}
	return prime_array[prime_index - 1];
}

int check_prime(int p, int *prime_array, int n) {
	for (int i = 0; i < n; i++) {
		if (p % prime_array[i] == 0) return 0;
		if (prime_array[n] * prime_array[n] >= p) return 1;
	}
	return 1;
}

void print_hash(void) {
	for (int i = 0; i < hash_table_size; i++) {
		if (hash_table[i].compressed_position != 0) {
			printf("%llu\n", hash_table[i].compressed_position);
		}
	}
}

Position p;

void set_test(void) {
	p.knights[0][0] = (Coord){4, 0};
	p.knights[0][1] = (Coord){4, 3};
	p.knights[0][2] = (Coord){4, 4};
	p.knights[1][0] = (Coord){0, 4};
	p.knights[1][1] = (Coord){2, 3};
	p.knights[1][2] = (Coord){4, 1};
	p.kings[0] = (Coord){2, 4};
	p.kings[1] = (Coord){0, 2};
	p.checks[0] = 0;
	p.checks[1] = 0;
	p.number_of_knights[0] = 3;
	p.number_of_knights[1] = 3;
	p.turn = BLACK;
	p.in_check = 0;
	p.checking_square = (Coord){0, 0};
}

int main(int argc, char **argv) {
	hash_table_size = 100000;
	hash_table_size = get_prime(hash_table_size);
	hash_table = calloc(hash_table_size, sizeof(Evaluated_Position));
	memset(str_pfx, ' ', sizeof(str_pfx));
	str_pfx[0] = '\0';
	setlocale(LC_ALL, "");
	set_test();
	//print_position(&p);
	//evaluate_all(&p, 5);
	Position position;
	memset(&position, 0, sizeof(position));
	get_starting_position(&position);
	print_position(&position);
	Evaluated_Move best_move;
	Position new_p;
	while (1) {
		print_position(&position);
		printf("Move: \n");
		Move move = get_user_move();
		Evaluated_Move cmp_response;
		make_move(&position, &new_p, &move);
		position = new_p;
		print_position(&position);
		clock_t t = clock();
		evaluate_all(&position, 8);
		cmp_response = get_best_move(&position, 8);
		printf("Time: %lu\n", clock() - t);
		make_move(&position, &new_p, &cmp_response.move);
		position = new_p;
		//printf("Evaluation: %d", cmp_response.evaluation);
		//print_hash();
		printf("%d/%d\n", hash_hit, hash_check);
	}
	return 0;
}