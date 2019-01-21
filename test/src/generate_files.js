const  fs = require('fs');
const fse = require('fs-extra')
const path = require('path');
const crypto = require('crypto');
const zlib = require('zlib');

function generate_file(filepath, filename, filecontentextended, emptyfile ) {
  return new Promise((resolve, reject) => {
    
    fse.outputFileSync(filepath+filename);

    var stream = fs.createWriteStream(filepath+filename);
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

function generate_manifest(filepath, manifestfile)
{
    return new Promise((resolve, reject) => {
        fse.outputFileSync(filepath+manifestfile);
    
        filewalker(filepath, function(err, data){
            if(err){
                throw err;
            }
            
            var stream = fs.createWriteStream(filepath+manifestfile);
            
            for (var foundfile of data) {
                if( filepath+manifestfile == foundfile)
                {
                    continue;
                } else {
                    const hash = crypto.createHash('sha256');
                    const input = fs.readFileSync(foundfile)
    
                    hash.update(input);
                    var update_subdirpath = filepath+"0.11.9-preview.1\\"
                    stream.write(`${hash.digest('hex')}`+" "+foundfile.substring(update_subdirpath.length)+"\n");
                    
                    const gzip = zlib.createGzip();
                    const inp = fs.createReadStream(foundfile);
                    const out = fs.createWriteStream(foundfile+".gz");
                    
                    inp.pipe(gzip).pipe(out);

                }
             }
            stream.end();
            stream.on("finish", () => { resolve(true); }); // not sure why you want to pass a boolean
            stream.on("error", reject); // don't forget this!
              
            //console.log(data);
        });

      });
}

exports.generate_server_dir = function (dirpath, manifestfile)
{
  var generatePromises = [];  
  var update_subdirpath = dirpath+"0.11.9-preview.1\\"
  generatePromises.push( generate_file(update_subdirpath, "test2.txt","new change", false) )
  generatePromises.push( generate_file(update_subdirpath, "file2.jpeg", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "file4.log.txt", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "file5.1", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "file 1.txt", "", false) )
  generatePromises.push( generate_file(update_subdirpath, "dir\\file6.ept", "", true) )
  var allPromise = Promise.all(generatePromises)
  allPromise.then(console.log, console.error)
  
  generatePromises = [];
  generatePromises.push( generate_manifest(dirpath, manifestfile ))
  var manifestPromise = Promise.all(generatePromises)
  manifestPromise.then(console.log, console.error)  

  console.log("finish generate_server_dir");
}

exports.generate_update_dir = function (dirpath)
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

  console.log("finish generate_update_dir");
}

exports.generate_result_dir = function (dirpath)
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

exports.clean_test_dir = function (dirpath)
{
  fse.removeSync(dirpath);
  console.log("finish clean_dir");
}
 