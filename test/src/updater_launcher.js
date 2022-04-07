const cp = require('child_process');
const path = require('path');
const fs = require('fs');

exports.start_updater = async function (testinfo) {
  const updaterPath = path.join(testinfo.updaterDir, testinfo.updaterName)
  const updaterPathE = updaterPath.replace(/\\/g, '\\\\') + "test";
  const updateDirE = testinfo.initialDir.replace(/\\/g, '\\\\');

  const updaterArgs = [
    '--base-url', `"${testinfo.serverUrl}"`,
    '--version', `"${testinfo.versionName}"`,
    '--exec', `"${updaterPathE}"`,
    '--cwd', `"${updateDirE}"`,
    '--interactive', `"${testinfo.runAsInteractive}"`,
    '--app-dir', `"${updateDirE}"`,
    '--force-temp'
  ];

  if (testinfo.pidWaiting) {
    testinfo.pidWaitingList.forEach((pid) => {
      updaterArgs.push('-p');
      updaterArgs.push(pid);
    });
  }

  if (testinfo.more_log_output)
    console.log(`SPAWN: args :\n${updaterArgs}`);

  if (testinfo.file_arguments) {
    var arg_file = fs.createWriteStream(testinfo.updateCfg);
    arg_file.on('error', function (err) { /* error handling */ });
    const updaterArgsPath = [`"${updaterPath}"`];
    var argumentsArray = updaterArgsPath.concat(updaterArgs);
    argumentsArray.forEach(function (v) { arg_file.write(v.replace(/"/g, '').replace(/\\\\/g, '\\') + '\n'); });
    //argumentsArray.forEach(function(v) { console.log(`SPAWN:`+v.replace(/"/g, '').replace(/\\\\/g, '\\')); });
    arg_file.end();
  }

  if (testinfo.wrong_arguments)
    updaterArgs.splice(0, updaterArgs.length);


  const app_spawned = cp.spawn(`${updaterPath}`, updaterArgs, {
    cwd: testinfo.updaterDir,
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

  if (testinfo.more_log_output)
    console.log(`SPAWN: promise: ${promise}`);

  app_spawned.unref();

  if (`${promise}` == "0") {
    return true;
  } else {
    return false;
  }
}
