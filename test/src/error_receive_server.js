const print_server_requests = false;
const custom_port = false;

//
var server;
 
exports.start_https_update_server = function (serverDir) {
    
}
 
exports.stop_https_update_server = function () {
  server.server.close()
}
