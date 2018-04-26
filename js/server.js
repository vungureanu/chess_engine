var cp = require("child_process");
const express = require("../node_modules/express");
const app = express();
var http = require("http").Server(app);
var io = require("../node_modules/socket.io")(http);

app.use(express.static("."));

io.on('connection', function(socket){
	var engine = cp.spawn("./a.out");
	engine.stdout.on("data", function(data_buf) {
		var data_str = data_buf.toString("utf8");
		var start = { row: parseInt(data_str[0]), col: parseInt(data_str[1]) };
		var end =  { row: parseInt(data_str[2]), col: parseInt(data_str[3]) };
		socket.emit("response", JSON.stringify({start: start, end: end}));
	});
	socket.on("move", function(move_str) {
		var move = JSON.parse(move_str);
		engine.stdin.write(move.start.row.toString() + move.start.col.toString() + move.end.row.toString() + move.end.col.toString() + "\n");		
	});
});

http.listen(3000, function(){
	console.log('listening on *:3000');
});

/*const engine = cp.spawn("./a.out");
engine.stdout.on("data", function(data_buf) {
	console.log("rec'v");
	var data_str = data_buf.toString("utf8");
	console.log(data_str);
	//var start = { row: parseInt(data_str[0]), col: parseInt(data_str[1]) };
	//var end =  { row: parseInt(data_str[2]), col: parseInt(data_str[3]) };
	//console.log(start, end);
});*/