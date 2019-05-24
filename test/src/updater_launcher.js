const cp = require('child_process');
const path = require('path');

exports.start_updater = async function (testinfo) {
  const updaterPath = path.join(testinfo.updaterDir, testinfo.updaterName)
  const updaterPathE = updaterPath.replace(/\\/g, '\\\\');
  const updateDirE = testinfo.initialDir.replace(/\\/g, '\\\\');

  const updaterArgs = [
    '--base-url', `"https://localhost/"`,
    '--version', `"${testinfo.versionName}"`,
    '--exec', `"${updaterPathE}"`,
    '--cwd', `"${updateDirE}"`,
    '--interactive', `"${testinfo.runAsInteractive}"`,
    '--app-dir', `"${updateDirE}"`,
    '--force-temp'
  ];
  if(testinfo.pidWaiting)
  {
    testinfo.pidWaitingList.forEach((pid) => {
      updaterArgs.push('-p');
      updaterArgs.push(pid);
  });
  }
  
  if(testinfo.more_log_output)
    console.log(`SPAWN: args :\n${updaterArgs}`);

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

  if(testinfo.more_log_output)
    console.log(`SPAWN: promise: ${promise}`);

  app_spawned.unref();

  if (`${promise}` == "0") {
    return true;
  } else {
    return false;
  }
}
