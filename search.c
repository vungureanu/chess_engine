#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <stdint.h>
#include <time.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <signal.h>

#define abs(x) ((x) < 0 ? -(x) : (x))
#define N 6
#define K (N-2)
#define WHITE 0
#define BLACK 1
#define HASH_DEPTH 5
#define NOT_IN_HASH 200 // Must be larger than greatest possible evaluation
#define IN_PROGRESS 201
#define ALPHA_REJECT -121
#define BETA_REJECT 121
#define MAX_DEPTH 100
#define HASH_FULL 202
#define DRAW 0
#define CAPTURE 1
#define CHECK 1
#define POSSIBLE_VALUES 4 // The number of possible values a move can have; used to determine the order in which moves are evaluated
#define SHALLOW_SEARCH_DEPTH 5 // Depth of a shallow search
#define SHALLOW_EXECUTION_DEPTH 8 // Least depth at which a shallow search is executed
#define FORCED_WIN_BLACK -101
#define FORCED_WIN_WHITE 101
#define BLACK_WINS -120
#define WHITE_WINS 120
#define MAX_MOVES 100

typedef struct Coord {
	int8_t row;
	int8_t col;
} Coord;

typedef struct Position { // Describes a position; 0 = white, 1 = black
	Coord knights[2][N-1];
	Coord kings[2];
	int8_t checks[2]; // Number of checks remaining
	int8_t number_of_knights[2]; // Number of knights remaining
	int8_t turn;
	int8_t in_check;
	Coord checking_square;
} Position;

typedef struct Move {
	Coord start;
	Coord end;
	int move_value;
} Move;

typedef struct Evaluated_Move {
	Move move;
	int evaluation;
} Evaluated_Move;

typedef struct Compressed_Position {
	uint32_t white_pieces;
	uint32_t black_pieces;
	uint8_t checks_and_turn;
} Compressed_Position;

typedef struct Evaluated_Position {
	Compressed_Position compressed_position;
	int evaluation;
	int8_t depth;
} Evaluated_Position;

typedef struct PDP {
	Position *pp;
	int depth;
	Evaluated_Move *ptr;
} PDP;

typedef struct LL_Node {
	Move *move;
	struct LL_Node *next_node;
} LL_Node;

typedef enum Mode {THREE_CHECKS, KINGS_CROSS} Mode;
typedef enum Move_Type {KING_MOVE, KNIGHT_MOVE} Move_Type;

int get_moves(Position *pp, Evaluated_Move *mp); // Adds moves to "mp" in decreasing order of expected value and returns number of moves added.
void make_move(Position *pp_old, Position *pp_new, Move *move); // Stores position which results from making given move in old position

int get_prime(int n);
int check_prime(int p, int *prime_array, int n);
// Used in creating a hash table of prime length to ensure more uniform distribution of hashes.

void print_position(Position *pp);
void print_em(Evaluated_Move em);

int shallow_reject(Position *pp, int alpha, int beta, int *flag, int *shallow_best);
// Evaluates a move at a shallow depth to determine whether it's worth exploring more thoroughly
int find_best_move(Position *pp, Move *mp, int alpha, int beta, int depth);
void *get_best_move_wrapper(void *position_depth_and_ptr);
// Examines position up to given depth and stores best move it finds in "mp".  Uses probabilistic cutting to
// reduce search space, and so may produce sub-optimal moves.  Its wrapper serves as a suitable entry point
// for newly created threads.

int equal_cmp(Compressed_Position *p1, Compressed_Position *p2); // Determines whether two positions are equal
int equal_crd(Coord *c1, Coord *c2); // Determines whether two coordinates are equal
void add_to_hash(Compressed_Position *compressed_position, int evaluation, int depth, int index);
int check_hash(Compressed_Position *compressed_position, int depth, int *index);
// Check if a position is in the hash table.  If so, return its evaluation; if not, and there is space,
// add it to the table and set "*index" accordingly.

int find_max_index(Evaluated_Move array[], int length);
int find_min_index(Evaluated_Move array[], int length);
// Return best moves from array (i.e., moves with greatest evaluation for White and smallest evaluation for Black)

void add_move(Coord *start, Coord *end, int value, Move *array, int n, LL_Node **roots, LL_Node *nodes);
// Adds move described by "start" and "end", with expected value "value", to array "array".
// Adds node to array "nodes" which points to move and to the previous node located at "roots[value]" (if any).
// Node at "roots[value]" changed to point to added node.  "roots" contains the at most "POSSIBLE_VALUES" nodes,
// the ith of which points to the first node in a linked list containing nodes pointing to moves with an
// expected value of i.

int parse_options(int argc, char **argv); // Allows user to set number of threads and hash table size.

int evaluate_position(Position *pp); // Gives rudimentary (depth-0) evaluation of position
int ev(Position *pp, Coord *start, Coord *end, Move_Type move_type);
// Returns an integer representing the promise of a candidate move (the greater the integer, the more promising the move)

int knight_attacks(Coord *knight_position, Coord *coord);
int king_attacks(Coord *king_position, Coord *coord);
int is_protected(Position *pp, Coord *coord);
int occupied_opponent(Position *pp, Coord *coord);
// Helper functions to determine legal moves

void set_pieces(uint32_t pieces, Position *pp, int color);
Position decompress_position(Compressed_Position *cmp);
Compressed_Position compress_position(Position *pp);
// Allow for the compression (for use in hash table) and decompression (for all other uses) of "Position" structures

int game_over(Position *pp, int available_moves, int *flag);
void check_if_game_over(Position *pp, int move_number, Compressed_Position *position_history);
// If the game has finished, exit and print the result

void standard_exit(int sig_num); // Close semaphore, free allocated memory, and exit

void update_status(int *move_number, Compressed_Position *position_history, Position *new_pp, Position *pp);
// Do some book-keeping to update game score (i.e., "position_history") and position

int positions_evaluated = 0;
Evaluated_Position *hash_table;
pthread_mutex_t *mutex_table;
int hash_table_size = 1000000;
int number_of_threads = 8;
int start_depth = 9;
Mode mode = THREE_CHECKS;
int verbose = 0;
sem_t *thread_num;

Compressed_Position compress_position(Position *pp) { // Associates each position with a unique 64-bit integer
	uint32_t cmp_white = 0, cmp_black = 0, square = 0;
	uint8_t checks_and_turn = 0;
	for (int i = 0; i < pp->number_of_knights[WHITE]; i++) {
		square = N * pp->knights[WHITE][i].row + pp->knights[WHITE][i].col + 1; // The +1 is there to prevent confusion between non-existent knights and knights located at (0, 0).
		cmp_white = cmp_white | (square << (i * 6)); // 2^6 = 64
	}
	square = N * pp->kings[WHITE].row + pp->kings[WHITE].col;
	cmp_white = cmp_white | (square << K * 6);
	for (int i = 0; i < pp->number_of_knights[BLACK]; i++) {
		square = N * pp->knights[BLACK][i].row + pp->knights[BLACK][i].col + 1;
		cmp_black = cmp_black | (square << (i * 6));
	}
	square = N * pp->kings[BLACK].row + pp->kings[BLACK].col;
	cmp_black = cmp_black | (square << (K * 6));
	checks_and_turn = (pp->turn) | (pp->checks[WHITE] << 1) | (pp->checks[BLACK] << 3);
	return (Compressed_Position){cmp_white, cmp_black, checks_and_turn};
}

Position decompress_position(Compressed_Position *cmp) {
	Position position;
	set_pieces(cmp->white_pieces, &position, WHITE);
	set_pieces(cmp->black_pieces, &position, BLACK);
	position.turn = cmp->checks_and_turn % 2;
	uint8_t mask = 3;
	position.checks[WHITE] = (cmp->checks_and_turn & (mask << 1)) >> 1;
	position.checks[BLACK] = (cmp->checks_and_turn & (mask << 3)) >> 3;
	for (int i = 0; i < position.number_of_knights[1 - position.turn]; i++) {
		if (knight_attacks(&position.knights[1 - position.turn][i], &position.kings[position.turn])) {
			position.in_check = 1;
			position.checking_square = position.knights[1 - position.turn][i];
		}
	}
	return position;
}

void set_pieces(uint32_t pieces, Position *pp, int color) {
	uint32_t mask = 63; // 2^6 - 1
	uint32_t knight_position = pieces & mask;
	int i = 0;
	while (knight_position != 0 && i < K) {
		int row = (int)(knight_position - 1) / N;
		int col = (int)(knight_position - 1) % N;
		pp->knights[color][i] = (Coord){row, col};
		i++;
		knight_position = (pieces & (mask << (i * 6))) >> (i * 6);
	}
	pp->number_of_knights[color] = i;
	uint32_t king_position = (pieces & (mask << (K * 6))) >> (K * 6);
	pp->kings[color] = (Coord){(int)king_position / N, (int)king_position % N};
}

int hash(Compressed_Position *compressed_position) {
	uint32_t result = compressed_position->white_pieces % hash_table_size;
	result *= (compressed_position->black_pieces % hash_table_size);
	result = (result * compressed_position->checks_and_turn) % hash_table_size;
	return (int)result;
}

int equal_cmp(Compressed_Position *p1, Compressed_Position *p2) {
	return (p1->white_pieces == p2->white_pieces) && (p1->black_pieces == p2->black_pieces) && (p1->checks_and_turn == p2->checks_and_turn);
}

int check_hash(Compressed_Position *compressed_position, int depth, int *index) {
	int p_hash = hash(compressed_position);
	int worst_index = p_hash;
	int worst_depth = MAX_DEPTH + 1;
	int evaluation;
	for (int i = 0; i < HASH_DEPTH; i++) {
		int index = (p_hash + i) % hash_table_size;
		pthread_mutex_lock(mutex_table + index);
		if (hash_table[index].evaluation != IN_PROGRESS) { // Do not overwrite pending evaluation
			if (hash_table[index].depth < worst_depth) {
				worst_depth = hash_table[index].depth;
				worst_index = index;
			}
			if (equal_cmp(&hash_table[index].compressed_position, compressed_position) && hash_table[index].depth >= depth) {
				evaluation = hash_table[index].evaluation;
				for (int j = 0; j <= i; j++) pthread_mutex_unlock(mutex_table + (p_hash + j) % hash_table_size);
				return evaluation;
			}
		}
	}
	if (worst_depth != MAX_DEPTH + 1) { // Space found in hash table
		hash_table[worst_index].compressed_position = *compressed_position;
		hash_table[worst_index].evaluation = IN_PROGRESS;
		for (int i = 0; i < HASH_DEPTH; i++) pthread_mutex_unlock(mutex_table + (p_hash + i) % hash_table_size);
		*index = worst_index;
		return NOT_IN_HASH;
	}
	for (int i = 0; i < HASH_DEPTH; i++) pthread_mutex_unlock(mutex_table + (p_hash + i) % hash_table_size);
	return HASH_FULL;
}

void add_to_hash(Compressed_Position *compressed_position, int evaluation, int depth, int index) {
	pthread_mutex_lock(mutex_table + index);
	hash_table[index].compressed_position = *compressed_position;
	hash_table[index].evaluation = evaluation;
	hash_table[index].depth = depth;
	pthread_mutex_unlock(mutex_table + index);
}

int find_max_index(Evaluated_Move array[], int length) { // Length must be greater than zero
	int max_index = 0;
	int max = array[0].evaluation;
	for (int i = 0; i < length; i++) {
		if (array[i].evaluation >= max) {
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
		if (array[i].evaluation <= min) {
			min = array[i].evaluation;
			min_index = i;
		}
	}
	return min_index;
}

void print_position(Position *pp) {
	Compressed_Position compressed_position = compress_position(pp);
	printf("Compressed position: %d %d %d\n", compressed_position.white_pieces, compressed_position.black_pieces, compressed_position.checks_and_turn);
	if (!verbose) return;
	int board[N][N];
	memset(board, 0, sizeof(board));
	board[pp->kings[0].row][pp->kings[0].col] = 9812;
	board[pp->kings[1].row][pp->kings[1].col] = 9818;
	for (int i = 0; i < pp->number_of_knights[0]; i++) board[pp->knights[0][i].row][pp->knights[0][i].col] = 9816;
	for (int i = 0; i < pp->number_of_knights[1]; i++) board[pp->knights[1][i].row][pp->knights[1][i].col] = 9822;
	for (int i = 0; i < N; i++) {
		printf("%c |", '0' + (N-i));
		for (int j = 0; j < N; j++) {
			if (board[i][j] != 0) printf("%lc|", board[i][j]);
			else printf(" |");
		}
		printf("\n");
	}
	printf("  ");
	for (int i = 0; i < N; i++) printf(" %c", 'a' + i);
	printf("\n");
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

int occupied_opponent(Position *pp, Coord *coord) {
	for (int i = 0; i < pp->number_of_knights[1-pp->turn]; i++) {
		if (coord->row == pp->knights[1-pp->turn][i].row && coord->col == pp->knights[1-pp->turn][i].col) return 1;
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

void add_move(Coord *start, Coord *end, int value, Move *array, int n, LL_Node **roots, LL_Node *nodes) {
	array[n].start = *start;
	array[n].end = *end;
	nodes[n].move = array + n;
	nodes[n].next_node = *(roots + value);
	*(roots + value) = nodes + n;
}

int ev(Position *pp, Coord *start, Coord *end, Move_Type move_type) {
	if (mode == THREE_CHECKS) {
		if (move_type == KING_MOVE) return occupied_opponent(pp, end);
		if (move_type == KNIGHT_MOVE) return occupied_opponent(pp, end) + knight_attacks(end, &(pp->kings[1-pp->turn]));
	}
	if (mode == KINGS_CROSS) {
		if (move_type == KING_MOVE) {
			int rows_forward = (pp->turn == WHITE) ? start->row - end->row : end->row - start->row;
			return occupied_opponent(pp, end) + rows_forward + 1;
		}
		if (move_type == KNIGHT_MOVE) return occupied_opponent(pp, end) + 1;
	}
	return 0;
}

int get_moves(Position *pp, Evaluated_Move *mp) {
	Coord move_array[8];
	LL_Node null_node = {NULL, NULL};
	LL_Node *roots[POSSIBLE_VALUES];
	for (int i = 0; i < POSSIBLE_VALUES; i++) roots[i] = &null_node;
	LL_Node nodes[8 * (K+1)];
	Move tmp_array[8 * (K+1)];
	int n = 0;
	if (pp->in_check) {
		// Determine whether checking knight can be captured by knight
		for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
			Coord *start = &(pp->knights[pp->turn][i]);
			Coord *end = &(pp->checking_square);
			if (knight_attacks(start, end)) {
				int value = ev(pp, start, end, KNIGHT_MOVE);
				add_move(start, end, value, tmp_array, n, roots, nodes);
				n++;
			}
		}
	}
	else {
		for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
			Coord *start = &(pp->knights[pp->turn][i]);
			int number_of_possible_moves = get_knight_moves(pp, start, move_array);
			for (int j = 0; j < number_of_possible_moves; j++) {
				int value = ev(pp, start, move_array + j, KNIGHT_MOVE);
				add_move(start, move_array + j, value, tmp_array, n, roots, nodes);
				n++;
			}
		}
	}
	int number_of_possible_moves = get_king_moves(pp, move_array);
	Coord *start = &(pp->kings[pp->turn]);
	for (int j = 0; j < number_of_possible_moves; j++) {
		int value = ev(pp, start, move_array + j, KING_MOVE);
		add_move(start, move_array + j, value, tmp_array, n, roots, nodes);
		n++;
	}
	int index = n-1;
	for (int i = 0; i < POSSIBLE_VALUES; i++) {
		LL_Node *ptr = *(roots + i);
		while (ptr->next_node != NULL) {
			mp[index].move = *(ptr->move);
			ptr = ptr->next_node;
			index--;
		}
	}
	return n;
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
		pp_new->checks[pp_new->turn] -= pp_new->in_check;
	}
}

int game_over(Position *pp, int available_moves, int *flag) {
	if (mode == THREE_CHECKS) {
		if (pp->checks[WHITE] == 0) {
			*flag = BLACK_WINS;
			return 1;
		}
		if (pp->checks[BLACK] == 0) {
			*flag = WHITE_WINS;
			return 1;
		}
		if (available_moves == 0) {
			if (pp->in_check) {
				if (pp->turn == WHITE) *flag = BLACK_WINS;
				else *flag = WHITE_WINS;
			}
			else *flag = DRAW;
			return 1;
		}
		if (pp->number_of_knights[WHITE] == 0 && pp->number_of_knights[BLACK] == 0) {
			*flag = DRAW;
			return 1;
		}
		return 0;
	}
	if (mode == KINGS_CROSS) {
		if (pp->kings[WHITE].row == 0) {
			*flag = WHITE_WINS;
			return 1;
		}
		if (pp->kings[BLACK].row == N-1) {
			*flag = BLACK_WINS;
			return 1;
		}
		if (available_moves == 0) {
			*flag = (pp->in_check) ? ((pp->turn == WHITE) ? BLACK_WINS : WHITE_WINS) : DRAW;
			return 1;
		}
		return 0;
	}
	return 0;
}

int evaluate_position(Position *pp) {
	if (mode == THREE_CHECKS) {
		return (2 * pp->number_of_knights[0] + pp->checks[0]) - (2 * pp->number_of_knights[1] + pp->checks[1]);
	}
	if (mode == KINGS_CROSS) {
		return (2 * pp->number_of_knights[WHITE] + (N - pp->kings[WHITE].row)) - (2 * pp->number_of_knights[BLACK] + pp->kings[BLACK].row + 1);
	}
	return 0;
}

void *get_best_move_wrapper(void *position_depth_and_ptrs) {
	PDP args = *((PDP *)position_depth_and_ptrs);
	Move best_response;
	(*(args.ptr)).evaluation = find_best_move(args.pp, &best_response, ALPHA_REJECT, BETA_REJECT, args.depth);
	sem_post(thread_num);
	return NULL;
}

Move evaluate_all(Position *pp, int depth) {
	Evaluated_Move em_array[8 * N];
	Position position_after_move[8 * N];
	int n = get_moves(pp, em_array); // Number of moves
	pthread_t tid[n];
	PDP args[n];
	sem_unlink("/semaphore");
	thread_num = sem_open("/semaphore", O_CREAT | O_EXCL, S_IRWXU, number_of_threads);
	if (thread_num == SEM_FAILED) {
		printf("Error establishing semaphore.\n");
		exit(1);
	}
	for (int i = 0; i < n; i++) {
		sem_wait(thread_num);
		make_move(pp, position_after_move + i, &em_array[i].move);
		args[i] = (PDP){position_after_move + i, depth, em_array + i};
		pthread_create(tid + i, NULL, get_best_move_wrapper, (void *)(args + i));
	}
	for (int i = 0; i < n; i++) pthread_join(tid[i], NULL);
	if (verbose) {
		for (int i = 0; i < n; i++) print_em(em_array[i]);
	}
	sem_close(thread_num);
	sem_unlink("/semaphore");
	int best_index = pp->turn == WHITE ? find_max_index(em_array, n) : find_min_index(em_array, n);
	int count = 0;
	int viable_indices[n];
	for (int i = 0; i < n; i++) {
		if (em_array[i].evaluation == em_array[best_index].evaluation) {
			viable_indices[count] = i;
			count++;
		}
	}
	return em_array[viable_indices[arc4random() % count]].move;
}

void print_em(Evaluated_Move em) {
	int evaluation = abs(em.evaluation);
	char flag[3] = "";
	if (em.evaluation >= 0) flag[0] = ' ';
	if (em.evaluation < 0) flag[0] = '-';
	if (em.evaluation >= FORCED_WIN_WHITE) {
		evaluation = WHITE_WINS - em.evaluation;
		flag[1] = '#';
	}
	if (em.evaluation <= FORCED_WIN_BLACK) {
		evaluation = em.evaluation - BLACK_WINS;
		flag[1] = '#';
	}
	printf("Evaluation: %s%d\t", flag, evaluation);
	char move[] = {0, 0, '-', 0, 0, 0};
	move[0] = 'a' + em.move.start.col;
	move[1] = '0' + N - em.move.start.row;
	move[3] = 'a' + em.move.end.col;
	move[4] = '0' + N - em.move.end.row;
	printf("Move: %s\n", move);
}

int find_best_move(Position *pp, Move *mp, int alpha, int beta, int depth) { // Returns the evaluation of White's best move from the position "*pp"
	positions_evaluated++;
	if (depth == 0) return evaluate_position(pp);
	Evaluated_Move em_array[8 * N];
	Position position_after_move;
	int n = get_moves(pp, em_array); // Number of candidate moves from current position
	int flag; // Value of finished game (White win, Black win, or draw)
	int shallow_best = (pp->turn == WHITE) ? ALPHA_REJECT : BETA_REJECT; // Best evaluation, at shallow depth, for a candidate move
	if (game_over(pp, n, &flag)) return flag;
	for (int i = 0; i < n; i++) { // Evaluate each possible move
		make_move(pp, &position_after_move, &em_array[i].move);
		if (depth >= SHALLOW_EXECUTION_DEPTH) {
			if (i == 0) shallow_reject(&position_after_move, ALPHA_REJECT, BETA_REJECT, &em_array[i].evaluation, &shallow_best);
			else if (shallow_reject(&position_after_move, alpha, beta, &em_array[i].evaluation, &shallow_best)) continue;
		}
		Compressed_Position compressed_position = compress_position(&position_after_move);
		int hash_index;
		int evaluation = check_hash(&compressed_position, depth, &hash_index);
		if (evaluation == NOT_IN_HASH) {
			em_array[i].evaluation = find_best_move(&position_after_move, mp, alpha, beta, depth - 1);
			if (em_array[i].evaluation != ALPHA_REJECT && em_array[i].evaluation != BETA_REJECT) {
				add_to_hash(&compressed_position, em_array[i].evaluation, depth, hash_index);
			}
		}
		else if (evaluation == HASH_FULL || evaluation == IN_PROGRESS) { // Proceed with evaluation, but do not add to hash
			em_array[i].evaluation = find_best_move(&position_after_move, mp, alpha, beta, depth - 1);
		}
		else { // Found in hash table
			em_array[i].evaluation = evaluation;
		}
		if (pp->turn == WHITE) {
			if (em_array[i].evaluation >= beta) return BETA_REJECT; // Black should reject this branch
			alpha = em_array[i].evaluation > alpha ? em_array[i].evaluation : alpha;
		}
		else {
			if (em_array[i].evaluation <= alpha) return ALPHA_REJECT; // White should reject this branch
			beta = em_array[i].evaluation < beta ? em_array[i].evaluation : beta;
		}
	}
	int best_index = (pp->turn == WHITE) ? find_max_index(em_array, n) : find_min_index(em_array, n);
	*mp = em_array[best_index].move;
	if (em_array[best_index].evaluation <= FORCED_WIN_BLACK) return em_array[best_index].evaluation + 1;
	if (em_array[best_index].evaluation >= FORCED_WIN_WHITE) return em_array[best_index].evaluation - 1;
	return em_array[best_index].evaluation;
}

int shallow_reject(Position *pp, int alpha, int beta, int *flag, int *shallow_best) {
	// We reject the position from the perspective of the side which has just moved (i.e., the side indicated by 1 - pp->turn)
	Move best_move;
	int evaluation = find_best_move(pp, &best_move, ALPHA_REJECT, BETA_REJECT, SHALLOW_SEARCH_DEPTH);
	if (pp->turn == BLACK) {
		if (evaluation < alpha && evaluation <= *shallow_best) {
			*flag = ALPHA_REJECT;
			return 1;
		}
		if (evaluation > *shallow_best) *shallow_best = evaluation;
	}
	if (pp->turn == WHITE) {
		if (evaluation > beta && evaluation >= *shallow_best) {
			*flag = BETA_REJECT;
			return 1;
		}
		if (evaluation < *shallow_best) *shallow_best = evaluation;
	}
	return 0;
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

int equal_crd(Coord *c1, Coord *c2) {
	return c1->row == c2->row && c1->col == c2->col;
}

Move get_user_move(Position *pp) {
	char buf[20] = {'\0'};
	int c1, r1, c2, r2;
	Move user_move;
	while (1) {
		loop: // This label is useful for breaking out of the inner "for" loop; a simple "continue" statement will not suffice
		if (verbose) printf("Enter move: \n");
		read(fileno(stdin), buf, 20);
		if (!verbose) {
			user_move = (Move){ (Coord){buf[0] - '0', buf[1] - '0'}, (Coord){buf[2] - '0', buf[3] - '0'} };
		}
		else {
			c1 = buf[0] - 'a';
			r1 = buf[1] - '0';
			c2 = buf[2] - 'a';
			r2 = buf[3] - '0';
			user_move = (Move){ (Coord){N-r1, c1}, (Coord){N-r2, c2} };
		}
		if (user_move.start.row < 0 || user_move.start.row >= N || user_move.start.col < 0 || user_move.start.col >= N) {
			printf("Illegal move (invalid starting square)\n");
			fflush(stdout);
			goto loop;
		}
		if (user_move.end.row < 0 || user_move.end.row >= N || user_move.end.col < 0 || user_move.end.col >= N) {
			printf("Illegal move (invalid ending square)\n");
			fflush(stdout);
			goto loop;
		}
		for (int i = 0; i < pp->number_of_knights[pp->turn]; i++) {
			Coord knight_coord = pp->knights[pp->turn][i];
			if (equal_crd(&knight_coord, &user_move.start)) {
				if (!knight_attacks(&knight_coord, &user_move.end)) {
					printf("Illegal move (knights don't move that way)\n");
					fflush(stdout);
					goto loop;
				}
				if (!pp->in_check || equal_crd(&pp->checking_square, &user_move.end)) {
					printf("Legal move\n");
					fflush(stdout);
					return user_move;
				}
				else {
					printf("Illegal move (you are in check)\n");
					fflush(stdout);
					goto loop;
				}
			}
		}
		Coord king_coord = pp->kings[pp->turn];
		if (equal_crd(&king_coord, &user_move.start)) {
			if (!king_attacks(&king_coord, &user_move.end)) {
				printf("Illegal move (kings don't move that way)\n");
				fflush(stdout);
				goto loop;
			}
			if (!is_protected(pp, &user_move.end)) {
				printf("Legal move\n");
				fflush(stdout);
				return user_move;
			}
			else {
				printf("Illegal move (cannot move into check)\n");
				fflush(stdout);
				goto loop;
			}
		}
		printf("Illegal move (you must move one of your own pieces)\n");
		fflush(stdout);
	}
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

void standard_exit(int sig_num) {
	sem_close(thread_num);
	free(hash_table);
	free(mutex_table);
	printf("\n");
	exit(0);
}

int parse_options(int argc, char **argv) {
	int option;
	long arg;
	while ((option = getopt(argc, argv, "h:t:d:mv")) != -1) {
		switch (option) {
			case 'h':
				arg = strtol(optarg, NULL, 10);
				if (arg <= 0 || arg > 1000000) printf("Invalid argument given to \"-h\".  Please enter an integer between 1 and 1000000.\n");
				else hash_table_size = get_prime((int)arg);
				break;
			case 't':
				arg = strtol(optarg, NULL, 10);
				if (arg <= 0 || arg > 64) printf("Invalid argument given to \"-t\".  Please enter an integer between 1 and 64.\n");
				else number_of_threads = (int)arg;
				break;
			case 'd':
				arg = strtol(optarg, NULL, 10);
				if (arg <= 0 || arg > 12) printf("Invalid argument given to \"-d\".  Please enter an integer between 1 and 12.\n");
				else start_depth = (int)arg;
				break;
			case 'm':
				mode = KINGS_CROSS;
				break;
			case 'v':
				verbose = 1;
				break;
			default:
				printf("Invalid argument.  Available options are -d, -h, -t, -v.\n");
				break;
		}
	}
	return 0;
}

void check_if_game_over(Position *pp, int move_number, Compressed_Position *position_history) {
	int flag;
	Evaluated_Move em_array[(K+1) * 8];
	if (move_number == MAX_MOVES) {
		printf("Result: Draw\n");
		standard_exit(0);
	}
	if (game_over(pp, get_moves(pp, em_array), &flag)) {
		switch (flag) {
			case WHITE_WINS:
				printf("Result: White wins\n");
				standard_exit(0);
				break;
			case BLACK_WINS:
				printf("Result: Black wins\n");
				standard_exit(0);
				break;
			case DRAW:
				printf("Result: Draw\n");
				standard_exit(0);
				break;
			default:
				printf("Error occurred in function game_over\n");
				break;
		}
	}
	int count = 0;
	for (int i = 0; i < move_number - 1; i++) {
		count += equal_cmp(position_history + i, position_history + (move_number-1));
		if (count == 2) { // Position has occurred two times before
			printf("Result: Draw\n");
			standard_exit(0);
		}
	}
}

void update_status(int *move_number, Compressed_Position *position_history, Position *new_pp, Position *pp) {
	position_history[*move_number] = compress_position(new_pp);
	(*move_number)++;
	*pp = *new_pp;
}

int main(int argc, char **argv) {
	parse_options(argc, argv);
	signal(SIGINT, standard_exit);
	hash_table = calloc(hash_table_size, sizeof(Evaluated_Position));
	mutex_table = calloc(hash_table_size, sizeof(pthread_mutex_t));
	for (int i = 0; i < hash_table_size; i++) pthread_mutex_init(mutex_table + i, NULL);
	setlocale(LC_ALL, ""); // Should allow for the display of UTF-8 characters (in particular, chess pieces)
	Position position;
	Position new_position;
	Move cmp_response; // Computer's response
	memset(&position, 0, sizeof(position));
	get_starting_position(&position);
	int flag = 0;
	Compressed_Position position_history[MAX_MOVES];
	position_history[0] = compress_position(&position);
	int move_number = 1; // Move number of the next move to be played
	printf("Ready\n");
	fflush(stdout);
	while (1) {
		if (verbose) print_position(&position);
		Move move = get_user_move(&position);
		make_move(&position, &new_position, &move);
		update_status(&move_number, position_history, &new_position, &position);
		if (verbose) print_position(&position);
		else if (position.in_check) {
			printf("Check %d\n", 1 - position.turn);
			fflush(stdout);
		}
		check_if_game_over(&position, move_number, position_history);
		cmp_response = evaluate_all(&position, start_depth);
		make_move(&position, &new_position, &cmp_response);
		update_status(&move_number, position_history, &new_position, &position);
		if (!verbose) {
			printf("Response %c%c%c%c\n", '0' + cmp_response.start.row, '0' + cmp_response.start.col, '0' + cmp_response.end.row, '0' + cmp_response.end.col);
			if (position.in_check) printf("Check %d\n", 1 - position.turn);
			fflush(stdout);
		}
		check_if_game_over(&position, move_number, position_history);
		memset(hash_table, 0, sizeof(Evaluated_Position) * hash_table_size);
	}
	return 0;
}