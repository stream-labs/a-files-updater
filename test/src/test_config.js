const path = require('path');
let test_counter = 1;

exports.gettestinfo = function (testname) {
  let newinfo = {
    testName:"",
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
    
    selfBlockingFile: false,
    selfLockingFile: false,

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
  newinfo.number = test_counter;
  test_counter = test_counter + 1;

  newinfo.testName = newinfo.number + testname;

  const testfilesDir = path.join(__dirname, "..", "testfiles", ""+newinfo.number);
  newinfo.serverDir = path.join(testfilesDir, "server")
  newinfo.initialDir = path.join(testfilesDir, "initial")
  newinfo.resultDir = path.join(testfilesDir, "result")
  newinfo.reporterDir = path.join(testfilesDir, "crash_reports")

  newinfo.updaterDir = path.join(__dirname, "..", "..", "build", "Debug")
  
  

  console.log("Test created: " + newinfo.testName );
  return newinfo;
}