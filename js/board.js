var N = 6;
var canvas;
var board_context;
var board_canvas;
var pieces_canvas;
var pieces_context;
var board_length = 300;
var square_length;
var board = [];
var socket = io();
const offset = 1.5;
var highlighted_squares = [null, null];
var pieces_loaded_event = new Event("pieces loaded");
addEventListener("pieces loaded", function() {
	set_element("board_container", 300, 6);
	initialize_board(6);
});

socket.on("response", function(response_str) {
	var response = JSON.parse(response_str);
	move(response.start, response.end);
});

function set_element(container_id, size, n) {
	var container = document.getElementById(container_id);
	container.style.position = "absolute";
	container.style.left = Math.floor(window.innerWidth / 2 - board_length / 2) + "px";
	board_canvas = document.createElement("canvas");
	board_canvas.style.position = "absolute";
	board_canvas.style.left = "0px";
	board_canvas.style.top = "0px";
	board_canvas.width = size;
	board_canvas.height = size;
	board_context = board_canvas.getContext("2d");
	container.appendChild(board_canvas);
	pieces_canvas = document.createElement("canvas");
	pieces_canvas.style.position = "absolute";
	pieces_canvas.style.left = "0px";
	pieces_canvas.style.top = "0px";
	pieces_context = pieces_canvas.getContext("2d");
	container.appendChild(board_canvas);
	container.appendChild(pieces_canvas);
	pieces_canvas.width = size;
	pieces_canvas.height = size;
	pieces_context.strokeStyle = "yellow";
	pieces_context.lineWidth = 2 * offset;
	pieces_canvas.onmousedown = function(e) {
		var col = Math.floor((e.pageX - container.offsetLeft) / square_length);
		var row = Math.floor((e.pageY - container.offsetTop) / square_length);
		if (highlighted_squares[0] == null) {
			highlighted_squares[0] = { row: row, col: col };
			pieces_context.strokeRect(col * square_length + offset, row * square_length + offset, square_length - 2 * offset, square_length - 2 * offset);
		}
		else if (highlighted_squares[0].row == row && highlighted_squares[0].col == col) {
			highlighted_squares[0] = null;
			pieces_context.clearRect(col * square_length, row * square_length, square_length, square_length);
			add_piece(board[row][col], row, col);
		}
		else if (highlighted_squares[1] == null) {
			highlighted_squares[1] = { row: row, col: col };
			pieces_context.strokeRect(col * square_length + offset, row * square_length + offset, square_length - 2 * offset, square_length - 2 * offset);
			move(highlighted_squares[0], highlighted_squares[1]);
			socket.emit("move", JSON.stringify({start: highlighted_squares[0], end: highlighted_squares[1]}));
			highlighted_squares = [null, null];
		}
	}
}

function initialize_board(n) {
	square_length = Math.floor(board_length / n);
	for (var i = 0; i < n; i++) {
		board.push([]);
		for (var j = 0; j < n; j++) {
			board[board.length-1].push([]);
			board_context.fillStyle = ((i + j) % 2 == 0) ? 'rgb(216, 163, 119)' : 'rgb(158, 108, 67)';
			board_context.fillRect(i * square_length, j * square_length, square_length, square_length);
		}
	}
	for (var i = 0; i < n-2; i++) {
		add_piece("black_knight", 0, i);
		add_piece("white_knight", n-1, n-i-1);
	}
	add_piece("black_king", 0, n-1);
	add_piece("white_king", n-1, 0);
}

function add_piece(piece_name, row, col) {
	board[row][col] = piece_name;
	pieces_context.drawImage(pieces.get(piece_name), col * square_length, row * square_length, square_length, square_length);
}

function move(start, end) {
	pieces_context.clearRect(start.col * square_length, start.row * square_length, square_length, square_length);
	pieces_context.clearRect(end.col * square_length, end.row * square_length, square_length, square_length);
	board[end.row][end.col] = board[start.row][start.col];
	pieces_context.drawImage(pieces.get(board[start.row][start.col]), end.col * square_length, end.row * square_length, square_length, square_length);
	board[start.row][start.col] = "";
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

const pieces = load_pieces();