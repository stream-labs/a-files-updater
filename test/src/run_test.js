const fs = require('fs');
const fse = require('fs-extra')
const cp = require('child_process');
const path = require('path');
const generate_files = require('./generate_files.js');
const updater_server = require('./updater_server.js');
const updater_launcher = require('./updater_launcher.js');

exports.test_update = async function (testinfo) {
  
  generate_files.generate_test_files(testinfo)
 
  if (testinfo.serverStarted) {
    updater_server.start_https_update_server(testinfo.serverDir);
  }

  if (testinfo.skipUpdaterLaunch) {
    return 0;
  }

  try {
    let launched = await updater_launcher.start_updater(testinfo)
    let ret = 0;

    if (!generate_files.check_results(testinfo)) {
      ret = 1;
      console.log("\nResult: after updater files not as expected");
    } else {
      console.log("\nResult: files as expected");
    }

    if (testinfo.not_keep_files) {
      generate_files.clean_test_dirs(testinfo);
    }

    updater_server.stop_https_update_server();

    return ret
  }
  catch (error) {
    updater_server.stop_https_update_server();
    //catch exception what was not catched by promises 
    console.log('SPAWN: error spawning catch. Error : ' + error);
    return 1
  }

}
