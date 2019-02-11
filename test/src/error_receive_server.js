const http = require('http');
const fs = require('fs');
const fse = require('fs-extra')
const path = require('path');

let server;

exports.start_crash_report_server = function (testinfo) {
    server = http.createServer(function (req, res) {
        console.log(req.method);
        console.log(req.headers);
        if (req.method == 'POST') {
            var body = '';
            req.on('data', function (data) {
                body += data;
            });
            req.on('end', function () {
                console.log("Body: " + body);
                filepath = path.join(testinfo.reporterDir, "crash_report.json")
                fse.outputFileSync(filepath);
                var stream = fs.createWriteStream(filepath);

                stream.write(body);

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

    server.listen(testinfo.reporterPort, testinfo.reporterHost);

    console.log('Listening at http://' + testinfo.reporterPort + ':' + testinfo.reporterPort);
}

exports.stop_crash_report_server = function () {
    server.close()
}
