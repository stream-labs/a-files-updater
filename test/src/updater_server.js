const print_server_requests = false;
const custom_port = false;

//
var server;

exports.start_http_update_server = function () {
  var http = require('http');

  var finalhandler = require('finalhandler');
  var serveStatic = require('serve-static');

  var serve = serveStatic(testinfo.serverDir);

  server = http.createServer(function (req, res) {
    var done = finalhandler(req, res);
    serve(req, res, done);
  });

  server.listen(8000);
}

exports.start_https_update_server = function (serverDir) 
{
  var bodyParser = require('body-parser')
  server = require("https-localhost")

  if (print_server_requests) {
    server.use(bodyParser())

    server.use((req, res) => {
      console.log(`SERVER REQUSTED: ${req.url}`);

      res.on("finish", () => {
        console.log(res);
      });

    });
  } else {
    server.serve(serverDir )
  }

  if (custom_port) {
    server.listen(8443); //updater not support custom ports 
  }
}

exports.stop_http_update_server = function () {
  server.close();
}

exports.stop_https_update_server = function () {
  server.server.close()
}
