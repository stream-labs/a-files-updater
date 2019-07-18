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
    appPathExtra: "OBS",
    versionName: "0.11.9-preview.1",
    updaterName: "slobs-updater.exe",

    serverUrl: "https://localhost/",

    files: [],

    //posible errors to test
    serverStarted: true,
    manifestGenerated: true,
    manifestWrongFile: false,
    blockFileFromUpdating: false,
    
    selfBlockingFile: false,
    selfLockingFile: false,
    selfBlockersCount: 1,
    
    pidWaiting: false,
    pidWaitingList: [],

    morebigfiles: false,
    
    let_404 : false,
    let_drop : false,
    let_wrong_header: false,
    let_5sec : false,
    let_15sec : false,
    max_trouble : 20,
    let_block_manifest: false,
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
    
    register_unnecesary_request : false,

    not_keep_files: false,
    more_log_output: false
  }
  newinfo.number = test_counter;
  test_counter = test_counter + 1;

  newinfo.testName = newinfo.number + testname;

  const testfilesDir = path.join(__dirname, "..", "testfiles", ""+newinfo.number);
  newinfo.serverDir = path.join(testfilesDir, "server")
  newinfo.initialDir = path.join(testfilesDir, "i件Яnitial"+newinfo.appPathExtra)
  newinfo.resultDir = path.join(testfilesDir, "result")
  newinfo.reporterDir = path.join(testfilesDir, "crash_reports")

  newinfo.updaterDir = path.join(__dirname, "..", "..", "build", "Debug")
 
  newinfo.files = [ 
    { name: "filea.exe",  hugefile: false, testing: "deleted empty" }, 
    { name: "file1.exe",  hugefile: false, testing: "deleted" }, 
    { name: "file2.txt",  hugefile: false, testing: "deleted" }, 
    { name: "file2.jpeg", hugefile: false, testing: "same" }, 
    { name: "file3.jpeg", hugefile: false, testing: "same" }, 
    { name: "Uninstall SLOBS.exe", hugefile: false, testing: "deleted exception" }, 
    { name: "руский файл.jpeg", hugefile: false, testing: "same" }, 
    { name: "文件名.jpeg", hugefile: false, testing: "same" }, 
    { name: "рузский файл.jpeg", hugefile: false, testing: "changed content" }, 
    { name: "文件名-2.jpeg", hugefile: false, testing: "changed content" }, 
    { name: "file 2.svn", hugefile: false, testing: "same empty" }, 
    { name: "resources/app.asar",  hugefile: true, testing: "changed content" }, 
    { name: "test2.txt",  hugefile: false, testing: "changed content" }, 
    { name: "dir/file3.zip",  hugefile: false, testing: "deleted" }, 
    { name: "file4.log.txt",   hugefile: false, testing: "created" }, 
    { name: "file5.1",   hugefile: false, testing: "created" }, 
    { name: "file 1.txt",  hugefile: false, testing: "created" }, 
    { name: "dir/file6.ept",   hugefile: false, testing: "created empty" }, 
  ];  

  console.log("Test created: " + newinfo.testName );
  return newinfo;
}