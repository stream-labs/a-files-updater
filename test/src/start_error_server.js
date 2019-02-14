//const error_server = require('./error_receive_server.js');

//error_server.start_https_update_server("");

const http = require('http');

const port = 80;
const host = 'localhost';

server = http.createServer(function (req, res) {
    console.log(req.method);
    console.log(req.headers);
    if (req.method == 'POST') {
        var body = '';
        req.on('data', function (data) {
            body += data;
            console.log("Partial body: " + body);
        });
        req.on('end', function () {
            console.log("Body: " + body);
        });
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end('{"version": "0"}\r\n');
    }
    else {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end('{"version": "0"}\r\n');
    }

});

server.listen(port, host);

console.log('Listening at http://' + host + ':' + port);