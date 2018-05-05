var N = 6;
var canvas;
var board_context;
var board_canvas;
var pieces_canvas;
var pieces_context;
var board_length;
var square_length;
var board = [];
const table_padding = 15;
const offset = 10;
const highlight_offset = 2;
const socket = io();
var highlighted_squares = [null, null];
var pieces_loaded_event = new Event("pieces_loaded");
var table = document.getElementById("game_info");
var white_checks = document.getElementById("white_checks");
var black_checks = document.getElementById("black_checks");
var new_three_checks = document.getElementById("new_three_checks");
var new_kings_cross = document.getElementById("new_kings_cross");
var status_text = document.getElementById("status_text");
status_text.innerHTML = "Click on one of the buttons below to start a new game."
new_three_checks.addEventListener("click", start_game.bind(null, "three_checks"));
new_kings_cross.addEventListener("click", start_game.bind(null, "kings_cross"));
var move_status = "wait";
var pending_move = null;

function start_game(game_type) {
	socket.emit("new_game", game_type);
	reset_pieces();
	status_text.innerHTML = "White to move";
	pending_move = null;
	move_status = "ready";
	white_checks.innerHTML = "White: 0";
	black_checks.innerHTML = "Black: 0";
	white_checks.style.visibility = (game_type == "three_checks") ? "visible" : "hidden";
	black_checks.style.visibility = (game_type == "three_checks") ? "visible" : "hidden";
}

function set_board_length(length) {
	board_length = length - (length % N);
	square_length = length / N;
}

addEventListener("pieces_loaded", function() {
	initialize_board("board_container", board_length, 6);
	reset_pieces();
	initialize_table();
});

socket.on("response", function(response_str) {
	status_text.innerHTML = "White to move"
	var response = JSON.parse(response_str);
	move(response.start, response.end);
});

socket.on("status", function(status_str) {
	status_text.innerHTML = status_str;
});

socket.on("legal", function() {
	status_text.innerHTML = "Thinking...";
	clear_highlight(pending_move.start);
	clear_highlight(pending_move.end);
	move(pending_move.start, pending_move.end);
	pending_move = null;
	move_status = "ready";
});

socket.on("illegal", function() {
	status_text.innerHTML = "Illegal move; White to move";
	clear_highlight(pending_move.start);
	clear_highlight(pending_move.end);
	pending_move = null;
	move_status = "ready";
});

socket.on("result", function(result) {
	move_status = "wait";
	status_text.innerHTML = result.trim();
	for (var square of highlighted_squares) {
		clear_highlight(square);
	}
});

socket.on("check", function(side) {
	if (side == "0") {
		white_checks.checks++;
		white_checks.innerHTML = "White: " + white_checks.checks;
	}
	else {
		black_checks.checks++;
		black_checks.innerHTML = "Black: " + black_checks.checks;
	}
});

function initialize_board(container_id, size, n) {
	var container = document.getElementById(container_id);
	container.style.position = "relative";
	container.style.left = Math.floor(window.innerWidth / 2 - board_length / 2) + "px";
	container.style.height = size + 2 * offset + "px";
	container.style.width = size + 2 * offset + "px";
	status_text.style.left = Math.floor(window.innerWidth / 2 - board_length / 2) + offset + "px";
	status_text.style.width = board_length + "px";
	board_canvas = document.createElement("canvas");
	var border_canvas = document.createElement("canvas");
	pieces_canvas = document.createElement("canvas");
	for (canvas of [board_canvas, border_canvas, pieces_canvas]) {
		canvas.style.position = "absolute";
		canvas.width = size + 2 * offset;
		canvas.height = size + 2 * offset;
		container.appendChild(canvas);
	}
	border_canvas.zIndex = "3";
	pieces_canvas.zIndex = "1";
	board_canvas.zIndex = "2";
	board_context = board_canvas.getContext("2d");
	var border_context = border_canvas.getContext("2d");
	border_context.strokeStyle = "black";
	border_context.lineWidth = 1;
	for (var i = 0; i <= N; i++) {
		border_context.beginPath();
		border_context.moveTo(offset + i * square_length, offset);
		border_context.lineTo(offset + i * square_length, size + offset + 2); // For some mysterious reason, it is necessary to add 2 for the line to reach the end of the board
		border_context.stroke();
		border_context.beginPath();
		border_context.moveTo(offset, offset + i * square_length);
		border_context.lineTo(size + offset + 2, offset + i * square_length);
		border_context.stroke();
	}
	pieces_context = pieces_canvas.getContext("2d");
	pieces_context.strokeStyle = "yellow";
	pieces_context.lineWidth = 2;
	pieces_canvas.onmousedown = mouse_down_on_board.bind(null, container);
	for (var i = 0; i < n; i++) {
		board.push([]);
		for (var j = 0; j < n; j++) {
			board[board.length-1].push("empty");
			board_context.fillStyle = ((i + j) % 2 == 0) ? 'rgb(216, 163, 119)' : 'rgb(158, 108, 67)';
			board_context.fillRect(offset + i * square_length, offset + j * square_length, square_length, square_length);
		}
	}
}

function highlight_square(row, col) {
	pieces_context.strokeRect(offset + highlight_offset + col * square_length, offset + highlight_offset + row * square_length, square_length - 2 * highlight_offset, square_length - 2 * highlight_offset);
}

function mouse_down_on_board(container, e) {
	var col = Math.floor((e.pageX - container.offsetLeft - offset - container.parentElement.offsetLeft) / square_length);
	var row = Math.floor((e.pageY - container.offsetTop - offset - container.parentElement.offsetTop) / square_length);
	if (move_status == "wait" || col < 0 || col >= N || row < 0 || row >= N) return;
	if (move_status == "ready") {
		move_status = "square_selected";
		highlighted_squares[0] = { row: row, col: col };
		highlight_square(row, col);
	}
	else if (move_status == "square_selected") {
		if (highlighted_squares[0].row == row && highlighted_squares[0].col == col) {
			move_status = "ready";
			clear_highlight(highlighted_squares[0]);
			highlighted_squares[0] = null;
		}
		else {
			move_status = "wait";
			highlighted_squares[1] = { row: row, col: col };
			highlight_square(row, col);
			pending_move = { start: highlighted_squares[0], end: highlighted_squares[1] };
			socket.emit("move", JSON.stringify(pending_move));
			highlighted_squares = [null, null];
		}
	}
}

function clear_highlight(square) {
	if (square == null) return;
	pieces_context.clearRect(offset + square.col * square_length, offset + square.row * square_length, square_length, square_length);
	if (board[square.row][square.col] != "empty") {
		add_piece(board[square.row][square.col], square.row, square.col);
	}
}

function reset_pieces() {
	for (var i = 0; i < N; i++) {
		for (var j = 0; j < N; j++) {
			board[i][j] = "empty";
			clear_highlight( {row: i, col: j} );
		}
	}
	for (var i = 0; i < N-2; i++) {
		add_piece("black_knight", 0, i);
		add_piece("white_knight", N-1, N-i-1);
	}
	add_piece("black_king", 0, N-1);
	add_piece("white_king", N-1, 0);
	white_checks.checks = 0;
	black_checks.checks = 0;
	white_checks.innerHTML = "";
	black_checks.innerHTML = "";
}

function initialize_table() {
	table.style.width = board_length + "px";
	table.style.overflow = "scroll";
	table.style.left = Math.floor(window.innerWidth / 2 - board_length / 2) + offset + "px";
}

function add_piece(piece_name, row, col) {
	board[row][col] = piece_name;
	pieces_context.drawImage(pieces.get(piece_name), offset + col * square_length, offset + row * square_length, square_length, square_length);
}

function move(start, end) {
	pieces_context.clearRect(offset + start.col * square_length, offset + start.row * square_length, square_length, square_length);
	pieces_context.clearRect(offset + end.col * square_length, offset + end.row * square_length, square_length, square_length);
	board[end.row][end.col] = board[start.row][start.col];
	pieces_context.drawImage(pieces.get(board[start.row][start.col]), offset + end.col * square_length, offset + end.row * square_length, square_length, square_length);
	board[start.row][start.col] = "empty";
}


function load_pieces() {
	var pieces_loaded = 0;
	var pieces = new Map();
	for (var i of ["white_knight", "white_king", "black_knight", "black_king"]) {
		var piece = new Image();
		piece.onload = function() {
			pieces_loaded++;
			if (pieces_loaded == 4) dispatchEvent(pieces_loaded_event);
		}
		piece.src = "img/" + i + ".png";
		pieces.set(i, piece);
	}
	return pieces;
}

set_board_length(500);
const pieces = load_pieces();