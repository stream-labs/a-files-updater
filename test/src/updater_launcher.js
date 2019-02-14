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
    '--interactive', `0`,
    '--app-dir', `"${updateDirE}"`,
    '--force-temp'
  ];
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

  console.log(`SPAWN: promise: ${promise}`);

  app_spawned.unref();

  if (`${promise}` == "0") {
    return true;
  } else {
    return false;
  }
}
