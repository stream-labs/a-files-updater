var generate_files = require('./generate_files.js');
const cp = require('child_process');
const path = require('path');

//options 
const print_server_requests = false;
const custom_port = false;
const not_keep_files = true;

const serverDir = "C:\\work\\Repos\\a-files-updater\\test\\testfiles\\server\\"
const updateDir = "C:\\work\\Repos\\a-files-updater\\test\\testfiles\\update\\"
const resultDir = "C:\\work\\Repos\\a-files-updater\\test\\testfiles\\result\\"

var updaterDir =  "C:\\work\\Repos\\a-files-updater\\build\\Debug\\"
var updaterPath = "C:\\work\\Repos\\a-files-updater\\build\\Debug\\slobs-updater.exe"


//
var server;

function generate_test_files()
{
  generate_files.generate_server_dir(serverDir, "0.11.9-preview.1.sha256");
  generate_files.generate_update_dir(updateDir);
  generate_files.generate_result_dir(resultDir);
}

function clean_test_dirs()
{
  generate_files.clean_test_dir(serverDir);
  generate_files.clean_test_dir(updateDir);
  generate_files.clean_test_dir(resultDir);
}


function start_http_update_server() {
  var http = require('http');

  var finalhandler = require('finalhandler');
  var serveStatic = require('serve-static');

  var serve = serveStatic(serverDir);

  server = http.createServer(function(req, res) {
    var done = finalhandler(req, res);
    serve(req, res, done);
  });

  server.listen(8000);
}

function start_https_update_server() {

  var bodyParser = require('body-parser') 
  server = require("https-localhost")

  if(print_server_requests)
  {
    server.use(bodyParser())

    server.use((req, res) => {
      console.log(`SERVER REQUSTED: ${req.url}`); 

      res.on("finish", () => {
        console.log(res);
      });

    });
  } else {
    server.serve(serverDir)
  }
  
  if(custom_port)
  {
    server.listen(8443); //updater not support custom ports 
  }
}

function stop_http_update_server()
{
  server.close();
}

function stop_https_update_server()
{
  
}

function check_results()
{
  var dircompare = require('dir-compare');
  var format = require('util').format;
   
  var options = {compareSize: true};
  var path1 = updateDir;
  var path2 = resultDir;
   
  var states = {'equal' : '==', 'left' : '->', 'right' : '<-', 'distinct' : '<>'};
   
  var res = dircompare.compareSync(path1, path2, options);
  console.log(format('equal: %s, distinct: %s, left: %s, right: %s, differences: %s, same: %s',
              res.equal, res.distinct, res.left, res.right, res.differences, res.same));

  res.diffSet.forEach(function (entry) {
      var state = states[entry.state];
      var name1 = entry.name1 ? entry.name1 : '';
      var name2 = entry.name2 ? entry.name2 : '';
      console.log(format('%s(%s)%s%s(%s)', name1, entry.type1, state, name2, entry.type2));
  });
  if(res.distinct !=0 && res.left != 0 || res.right != 0 || res.differences != 0)
  {
    return false;
  } else {
    return true;
  }
}

async function start_updater()
{
    var updaterPathE = updaterPath.replace(/\\/g, '\\\\');
    var updateDirE = updateDir.replace(/\\/g, '\\\\');
    
    const updaterArgs = [
    '--base-url', `"https://localhost/"`,
    '--version', `"0.11.9-preview.1"`,
    '--exec',`"${updaterPathE}"`,
    '--cwd', `"${updateDirE}"`,
    '--app-dir', `"${updateDirE}"`,
    '--force-temp'
  ];
  console.log(`SPAWN: args :\n${updaterArgs}`);

  const app_spawned = cp.spawn(`${updaterPath}`, updaterArgs, {
    cwd: updaterDir,
    detached: false, 
    shell: true,
    windowsVerbatimArguments: true
  });
  
  app_spawned.stdout.on('data', (data) => {
  //  console.log(`SPAWN: stdout:\n${data}`);
  });

  //make promises for app exit , error
  const primiseExit = new Promise(resolve => {
    app_spawned.on('exit', resolve);
  });

  const primiseError = new Promise(resolve => {
    app_spawned.on('error', resolve);
  });

  var promise = await Promise.race([primiseError, primiseExit]);
  
  console.log(`SPAWN: promise:\n${promise}`);

  app_spawned.unref();
  if(`${promise}` == "0" )
  {
    return true;
  } else {
    return false;
  }
}

function test_good_update()
{
  clean_test_dirs();

  generate_test_files()

  start_https_update_server();

  start_updater().then(status => {
    var ret = 0;
    if( !check_results() )
    {
      ret = 1;
    }

    if(not_keep_files)
    {
      clean_test_dirs();
    }

    stop_https_update_server();
    
    process.exit(ret);

  }).catch((error) => {
    //catch exception what was not catched by promises 
    console.log('SPAWN: error spawning catch. Error : ' + error);
    process.exit(1)
  })

}

test_good_update();