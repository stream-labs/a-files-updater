var http = require('http');
var https = require('https');
var httpProxy = require('http-proxy');
var finalhandler = require('finalhandler');
var serveStatic = require('serve-static');

const fs = require('fs'); 
 
var server;
var proxy;
var proxyServer;

const let_404 = true;
const let_drop = false;
const let_5sec = false;
const let_15sec = false;

const let_block_one_file = true;
const block_file_number = 10 ;
var files_served = 0;
var file_to_block;

const max_trouble = 20;
var have_trouble = 0;


exports.start_https_update_server = function (serverDir) {
  var options = {
    key: fs.readFileSync('valid-ssl-key.pem', 'utf8'),
    cert: fs.readFileSync('valid-ssl-cert.pem', 'utf8')
  };

  proxy = httpProxy.createProxyServer();

  proxyServer = https.createServer(options, function (req, res) {
    let test_timeout = 100 + Math.floor(Math.random() * Math.floor(500));
    let chanse_for_trouble = Math.floor(Math.random() * Math.floor(100));
    
    let do_block = (chanse_for_trouble > 90 && have_trouble < max_trouble ) ;

    if((let_block_one_file && (block_file_number == files_served) ) && ! file_to_block)
    {
      file_to_block = req.url;
    }
    if(let_block_one_file && ( file_to_block === req.url ))
    {
      do_block = true;
    }

    if( do_block )
    {
      have_trouble = have_trouble + 1;
      if(let_404)
      {
        console.log("Will 404 this request " + req.url);
        res.writeHead(404, {'Content-Type': 'text/html'});
        res.write('Hello World!');  
        res.end();
        return;
      } else if(let_drop)
      {
        console.log("Will drop this request " + req.url);
        res.end();
        return;
      } else if(let_5sec)
      {
        testTimeout = 5*1000+100;
        console.log("Will make this request delayed for "+test_timeout + "ms, " + req.url);
      } else if(let_15sec)
      {
        testTimeout = 10*1000+100;
        console.log("Will make this request delayed for "+test_timeout + "ms, " + req.url);
      }
    } else {
      console.log("Will make this request delayed for "+test_timeout + "ms, " + req.url);
    }

    files_served = files_served + 1;

    setTimeout(function () {
      proxy.web(req, res, {
        target: 'http://localhost:8443',
      });
    }, test_timeout);    // This simulates an operation that takes XXXms to execute
  });

  proxyServer.listen(443);

  var serve = serveStatic(serverDir);
  server = http.createServer(function (req, res) {
    var done = finalhandler(req, res);
    serve(req, res, done);
  });

  server.listen(8443);
  
  console.log('Update server emulator listening at http://' + 'localhost' + ':' + '443');
}

exports.stop_https_update_server = function () {
  server.close()
  proxyServer.close();
}
