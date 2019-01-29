const path = require('path');

exports.gettestinfo = function ()
{
  let newinfo = {
    serverDir : "",
    initialDir : "",
    resultDir : "",
    updaterDir : "",
    versionName : "0.11.9-preview.1" ,
    updaterName : "slobs-updater.exe",

    serverStarted : true,
    manifestGenerated : true, 
    manifestWrongFile : false, 
    skipUpdaterLaunch : false,
    
    expectedResult : "filesupdated", // "filesnotchanged", ""

    not_keep_files : true
  }

  const testfilesDir = path.join(__dirname, "..", "testfiles");
  newinfo.serverDir = path.join(testfilesDir, "server")
  newinfo.initialDir = path.join(testfilesDir, "initial")
  newinfo.resultDir = path.join(testfilesDir, "result")
 
  newinfo.updaterDir = path.join(__dirname, "..", "..", "build", "Debug")
  
  return newinfo;
}