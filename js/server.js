var cp = require("child_process");
const express = require("../node_modules/express");
const app = express();
var http = require("http").Server(app);
var io = require("../node_modules/socket.io")(http);
var engines = new Map();
var tot_engines = 0;

app.use(express.static("."));

process.on('exit', function() {
	engines.forEach(function(value, key, map) {
		value.kill("SIGINT");
	});
	console.log("Goodbye");
});

process.on('SIGINT', process.exit);

function parse_engine_data(engine, socket, data_str) {
	if (data_str == "") return;
	console.log("Sent to client:", data_str);
	if (data_str.substring(0, 5) == "Legal") {
		socket.emit("legal");
	}
	else if (data_str.substring(0, 7) == "Illegal") {
		socket.emit("illegal");
	}
	else if (data_str.substring(0, 8) == "Response") {
		var move = data_str.split(" ")[1];
		var start = { row: parseInt(move[0]), col: parseInt(move[1]) };
		var end =  { row: parseInt(move[2]), col: parseInt(move[3]) };
		socket.emit("response", JSON.stringify({start: start, end: end}));
	}
	else if (data_str.substring(0, 6) == "Result") {
		socket.emit("result", data_str.split(":")[1].trim());
		engine.kill("SIGINT");
	}
	else if (data_str.substring(0, 5) == "Check") {
		socket.emit("check", data_str.split(" ")[1]);
	}
}

io.on('connection', function(socket){
	var engine;
	var engine_num = tot_engines;
	tot_engines++;
	socket.on("move", function(move_str) {
		console.log("Received move:", move_str);
		var move = JSON.parse(move_str);
		try {
			if (engine != null) engine.stdin.write(move.start.row.toString() + move.start.col.toString() + move.end.row.toString() + move.end.col.toString() + "\n");		
		}
		catch(error) {
			console.log(error);
			if (engine != null) engine.kill("SIGINT");
		}
	});
	socket.on("disconnect", function() {
		if (engine != null) engine.kill("SIGINT");
		engines.delete(engine_num);
	});
	socket.on("new_game", function(game_type) {
		if (engine != null) engine.kill("SIGINT");
		if (game_type == "three_checks") {
			engine = cp.spawn("./a.out");
		}
		else {
			engine = cp.spawn("./a.out", ["-m "]);
		}
		engines.set(engine_num, engine);
		engine.stdout.on("data", function(data_buf) {
			data = data_buf.toString("utf8").split("\n");
			data.forEach( (data_str) => parse_engine_data(engine, socket, data_str) );
		});
	});
});

http.listen(3000, function(){
	console.log('listening on *:3000');
});