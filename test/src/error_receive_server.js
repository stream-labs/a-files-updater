const fs = require('fs');
const fse = require('fs-extra')
const path = require('path');
var http = require('http');
var https = require('https');
var httpProxy = require('http-proxy');
var finalhandler = require('finalhandler');

let server;
var proxy;
var proxyServer;

var ssl_options = {
    key: fs.readFileSync('valid-ssl-key.pem', 'utf8'),
    cert: fs.readFileSync('valid-ssl-cert.pem', 'utf8')
  };
  
exports.start_crash_report_server = function (testinfo) {
    proxy = httpProxy.createProxyServer();

    proxy.on('error', function (err, req, res) 
    { 
        //console.log("just proxy error");
    });

    proxyServer = https.createServer(ssl_options, function (req, res) {
        setTimeout(function () {
            proxy.web(req, res, {
              target: 'http://localhost:9443',
            });
        }, 0);    
    });

    proxyServer.listen(1443);

    server = http.createServer(function (req, res) {
        console.log(req.method);
        console.log(req.headers);
        if (req.method == 'POST') {
            var body = '';
            req.on('data', function (data) {
                body += data;
            });
            req.on('end', function () {
                //console.log("Body: " + body);
                filepath = path.join(testinfo.reporterDir, "crash_report.json")
                fse.outputFileSync(filepath);
                var stream = fs.createWriteStream(filepath);

                stream.write(body);
                
                console.log("body size "+body.length)

                stream.end();
                stream.on("finish", () => { });
                stream.on("error",  () => {
                    console.log("failed to write report")
                 });
            });
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end('{"version": "0"}\r\n');
        }
        else {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end('{"version": "0"}\r\n');
        }

    });

    server.listen(9443, testinfo.reporterHost);

    console.log('Sentry emulator listening at http://' + testinfo.reporterHost + ':' + 1443);
}

exports.stop_crash_report_server = function () {
    server.close()
}
