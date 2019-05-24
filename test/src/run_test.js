const fs = require('fs');
const fse = require('fs-extra')
const cp = require('child_process');
const path = require('path');

const generate_files = require('./generate_files.js');
const updater_server = require('./updater_server.js');
const reporter_server = require('./error_receive_server.js');
const updater_launcher = require('./updater_launcher.js');

exports.test_update = async function (testinfo) {
  if(testinfo.more_log_output)
    console.log("--- Test files will be generated.");

  await generate_files.generate_test_files(testinfo)
  
  if(testinfo.more_log_output)
    console.log("--- Test server will be started.");

  if (testinfo.serverStarted) {
    updater_server.start_https_update_server(testinfo);

    reporter_server.start_crash_report_server(testinfo);
  }

  if (testinfo.skipUpdaterLaunch) {
    return 0;
  }
  
  if(testinfo.more_log_output)
    console.log("--- Ready to start updater.");
  try {
    let launched = await updater_launcher.start_updater(testinfo)
    let ret = 0;

    if (!generate_files.check_results(testinfo)) {
      ret = 1;
      console.log("=== Test "+testinfo.number+" result: after updater files not as expected");
    } else {
      if( testinfo.register_unnecesary_request > 0)
      {
        ret = 1;
        console.log("=== Test "+testinfo.number+" result: unchanged files requested from server");
      } else {
        console.log("=== Test "+testinfo.number+" result: files as expected");
      }
    }


    updater_server.stop_https_update_server();
    reporter_server.stop_crash_report_server();

    generate_files.clean_after_test(testinfo, false);

    return ret
  }

  catch (error) {
    updater_server.stop_https_update_server();
    reporter_server.stop_crash_report_server();
    //catch exception what was not catched by promises 
    console.log('SPAWN: error spawning catch. Error : ' + error);
    return 1
  }

}
