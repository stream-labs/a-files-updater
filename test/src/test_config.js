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

    reporterHost: "localhost",
    reporterPort: 80,

    skipUpdaterLaunch: false,

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