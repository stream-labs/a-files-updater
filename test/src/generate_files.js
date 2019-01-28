const  fs = require('fs');
const fse = require('fs-extra')
const path = require('path');
const crypto = require('crypto');
const zlib = require('zlib');

function generate_file(filedir, filename, filecontentextended, emptyfile ) {
  return new Promise((resolve, reject) => {
    const filepath = path.join(filedir, filename)
    fse.outputFileSync(filepath);

    var stream = fs.createWriteStream(filepath);
    if(!emptyfile)
    {
      var i;
      for (i = 0; i < 1000; i++) { 
          stream.write(filename+filecontentextended);
      }
    }

    stream.end();
    stream.on("finish", () => { resolve(true); }); // not sure why you want to pass a boolean
    stream.on("error", reject); // don't forget this!

  });
}

function filewalker(dir, done) {
    let results = [];

    fs.readdir(dir, function(err, list) {
        if (err) return done(err);

        var pending = list.length;

        if (!pending) return done(null, results);

        list.forEach(function(file){
            file = path.resolve(dir, file);

            fs.stat(file, function(err, stat){
                // If directory, execute a recursive call
                if (stat && stat.isDirectory()) {
                    //results.push(file);

                    filewalker(file, function(err, res){
                        results = results.concat(res);
                        if (!--pending) done(null, results);
                    });
                } else {
                    results.push(file);

                    if (!--pending) done(null, results);
                }
            });
        });
    });
};

function generate_manifest(testinfo)
{
    return new Promise((resolve, reject) => {
      const filepath = path.join(testinfo.serverDir,  testinfo.versionName+".sha256" )
        fse.outputFileSync(filepath );
    
        filewalker(testinfo.serverDir, function(err, data){
            if(err){
                throw err;
            }
            
            var stream = fs.createWriteStream(filepath);
            
            for (var foundfile of data) {
                if( filepath == foundfile)
                {
                    continue;
                } else {
                    const hash = crypto.createHash('sha256');
                    const input = fs.readFileSync(foundfile)
    
                    hash.update(input);
                    var update_subdirpath = path.join( testinfo.serverDir, testinfo.versionName)
                    stream.write(`${hash.digest('hex')}`+" "+foundfile.substring(update_subdirpath.length)+"\n");
                    
                    const gzip = zlib.createGzip();
                    const inp = fs.createReadStream(foundfile);
                    const out = fs.createWriteStream(foundfile+".gz");
                    
                    inp.pipe(gzip).pipe(out);

                }
            }
            if(testinfo.manifestWrongFile)
            {
               
              stream.write(`e980fe14384b38340fad866a92f2cbe4aeef268fac3368274bcb0b8e2cd32702`+" \\missing_file5.1\n");
            }
            stream.end();
            stream.on("finish", () => { resolve(true); }); // not sure why you want to pass a boolean
            stream.on("error", reject); // don't forget this!
              
            //console.log(data);
        });

      });
}

function generate_server_dir(testinfo)
{
  var generatePromises = [];  
  const update_subdirpath = path.join( testinfo.serverDir,testinfo.versionName)
  generatePromises.push( generate_file(update_subdirpath, "test2.txt","new change", false) )
  generatePromises.push( generate_file(update_subdirpath, "file2.jpeg", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "file4.log.txt", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "file5.1", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "file 1.txt", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "dir\\file6.ept", "", true) )
  var allPromise = Promise.all(generatePromises)
  allPromise.then(console.log, console.error)
  
  if(testinfo.manifestGenerated)
  {
    generatePromises = [];
    generatePromises.push( generate_manifest(testinfo))
    var manifestPromise = Promise.all(generatePromises)
    manifestPromise.then(console.log, console.error)  
  }

  console.log("finish generate_server_dir");
}

function generate_initial_dir(dirpath)
{
  var generatePromises = [];  
  generatePromises.push( generate_file(dirpath, "filea.exe","", true) )
  generatePromises.push( generate_file(dirpath, "file1.exe","", false) )
  generatePromises.push( generate_file(dirpath, "file2.txt", "", false) )
  generatePromises.push( generate_file(dirpath, "file2.jpeg", "", false) )
  generatePromises.push( generate_file(dirpath, "test2.txt","old content", false) )
  generatePromises.push( generate_file(dirpath, "dir\\file3.zip", "", false) )
  var allPromise = Promise.all(generatePromises)
  allPromise.then(console.log, console.error)

  console.log("finish generate_initial_dir");
}

function generate_result_dir(dirpath)
{
  var generatePromises = [];  
  
  generatePromises.push( generate_file(dirpath, "filea.exe","", true) )
  generatePromises.push( generate_file(dirpath, "file1.exe","", false) )
  generatePromises.push( generate_file(dirpath, "file2.txt", "", false) )
  generatePromises.push( generate_file(dirpath, "file2.jpeg", "", false) )
  generatePromises.push( generate_file(dirpath, "test2.txt","new change", false) )
  generatePromises.push( generate_file(dirpath, "file4.log.txt", "", false) )
  generatePromises.push( generate_file(dirpath, "file5.1", "", false) )
  generatePromises.push( generate_file(dirpath, "file 1.txt", "", false) )
  generatePromises.push( generate_file(dirpath, "dir\\file3.zip", "", false) )
  generatePromises.push( generate_file(dirpath, "dir\\file6.ept", "", true) )
  var allPromise = Promise.all(generatePromises)
  allPromise.then(console.log, console.error)

  console.log("finish generate_result_dir");
}

clean_test_dir = function (dirpath)
{
  fse.removeSync(dirpath);
  console.log("finish clean_dir");
}

exports.clean_test_dirs = function (testinfo)
{
  clean_test_dir(testinfo.serverDir);
  clean_test_dir(testinfo.initialDir);
  clean_test_dir(testinfo.resultDir);
}

exports.generate_test_files = function (testinfo)
{
  exports.clean_test_dirs(testinfo);

  generate_server_dir(testinfo);
  generate_initial_dir(testinfo.initialDir);
  
  if(testinfo.expectedResult == "filesupdated")
    generate_result_dir(testinfo.resultDir);
  if(testinfo.expectedResult == "filesnotchanged")
    generate_initial_dir(testinfo.resultDir);
}
 
exports.check_results = function (testinfo)
{
  var dircompare = require('dir-compare');
  var format = require('util').format;
   
  var options = {compareSize: true};
  var path1 = testinfo.initialDir;
  var path2 = testinfo.resultDir;
   
  var states = {'equal' : '==', 'left' : '->', 'right' : '<-', 'distinct' : '<>'};
   
  var res = dircompare.compareSync(path1, path2, options);
  const ret = res.distinct !=0 && res.left != 0 || res.right != 0 || res.differences != 0

  console.log(format('Compare dirs: equal=%s, distinct=%s, left=%s, right=%s, differences=%s, same=%s',
              res.equal, res.distinct, res.left, res.right, res.differences, res.same));

  res.diffSet.forEach(function (entry) {
      var state = states[entry.state];
      var name1 = entry.name1 ? entry.name1 : '';
      var name2 = entry.name2 ? entry.name2 : '';
      
      if(ret)
      {
        console.log(format('%s(%s)%s%s(%s)', name1, entry.type1, state, name2, entry.type2));
      }
  });
  
  return !ret;
  
}
