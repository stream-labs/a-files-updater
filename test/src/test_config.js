const path = require('path');

exports.gettestinfo = function () {
  let newinfo = {
    serverDir: "",
    initialDir: "",
    resultDir: "",
    updaterDir: "",
    versionName: "0.11.9-preview.1",
    updaterName: "slobs-updater.exe",

    //posible errors to test
    serverStarted: true,
    manifestGenerated: true,
    manifestWrongFile: false,
    blockFileFromUpdating: false,

    skipUpdaterLaunch: false,

    expectedResult: "filesupdated", // "filesnotchanged", ""

    not_keep_files: false
  }

  const testfilesDir = path.join(__dirname, "..", "testfiles");
  newinfo.serverDir = path.join(testfilesDir, "server")
  newinfo.initialDir = path.join(testfilesDir, "initial")
  newinfo.resultDir = path.join(testfilesDir, "result")

  newinfo.updaterDir = path.join(__dirname, "..", "..", "build", "Debug")

  return newinfo;
}