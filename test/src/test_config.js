const path = require('path');

exports.gettestinfo = function () {
  let newinfo = {
    serverDir: "",
    initialDir: "",
    resultDir: "",
    updaterDir: "",
    reporterDir: "",
    versionName: "0.11.9-preview.1",
    updaterName: "slobs-updater.exe",

    //posible errors to test
    serverStarted: true,
    manifestGenerated: true,
    manifestWrongFile: false,
    blockFileFromUpdating: false,
    morebigfiles: false,

    let_404 : false,
    let_drop : false,
    let_wrong_header: false,
    let_5sec : false,
    let_15sec : false,
    max_trouble : 20,

    let_block_one_file : false,
    block_file_number : 10,
    
    //sentry emulator
    reporterHost: "localhost",
    reporterPort: 80,

    //how to run updater 
    skipUpdaterLaunch: false,
    runAsInteractive: '0',

    //test results 
    expectedResult: "filesupdated", // "filesnotchanged", ""
    expectedCrashReport: false,

    
    not_keep_files: false
  }

  const testfilesDir = path.join(__dirname, "..", "testfiles");
  newinfo.serverDir = path.join(testfilesDir, "server")
  newinfo.initialDir = path.join(testfilesDir, "initial")
  newinfo.resultDir = path.join(testfilesDir, "result")
  newinfo.reporterDir = path.join(testfilesDir, "crash_reports")

  newinfo.updaterDir = path.join(__dirname, "..", "..", "build", "Debug")

  return newinfo;
}