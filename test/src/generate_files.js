const fs = require('fs');
const fse = require('fs-extra')
const path = require('path');
const crypto = require('crypto');
const zlib = require('zlib');

const hugefilecontent = "asdfasdf23rdsf34rsdfasdfasdf23rdsf34rsdfasdfasdf23rdsf34rsdfasdfasdf23rdsf34rsdf"
const morefilesmax = 128;
const filecontentlines = 100;
const hugefileincrese = 20;

async function generate_file(filedir, filename, filecontentextended = "", emptyfile = false, hugefile = false) 
{
  return new Promise((resolve, reject) => {
    const filepath = path.join(filedir, filename)
    fse.outputFileSync(filepath);

    var stream = fs.createWriteStream(filepath);
    if (!emptyfile) 
    {
      var i;
      var lines = filecontentlines;
      if(hugefile) 
      {
        filecontentextended = hugefilecontent;
        lines = filecontentlines*hugefileincrese;
      }
      stream.write(filename + `\n`);
      
      var cipher = crypto.createCipheriv('aes-256-ctr', 'b2df428b9929d3ace7c598bbf4e496b2', 'dkfirosnfkdyrifj')

      for (i = 0; i < lines; i++) 
      {
        var crypted = cipher.update(filecontentextended+i,'utf8','hex')
        stream.write(i + ` ${crypted}` + `\n`);
      }
    }

    stream.end();
    stream.on("finish",  () => { resolve(true); }); 
    stream.on("error", reject); 

  });
}

function generate_file_sync(filedir, filename, filecontentextended = "", emptyfile = false, hugefile = false) 
{
    const filepath = path.join(filedir, filename)
    fse.outputFileSync(filepath);

    var stream = fs.openSync(filepath);
    if (!emptyfile) 
    {
      var i;
      var lines = filecontentlines;
      if(hugefile) 
      {
        filecontentextended = hugefilecontent;
        lines = filecontentlines*hugefileincrese;
      }
      stream.writeSync(filename + `\n`);
      
      var cipher = crypto.createCipheriv('aes-256-ctr', 'b2df428b9929d3ace7c598bbf4e496b2', 'dkfirosnfkdyrifj')

      for (i = 0; i < lines; i++) 
      {
        var crypted = cipher.update(filecontentextended+i,'utf8','hex')
        stream.writeSync(i + ` ${crypted}` + `\n`);
      }
    }

    stream.end();
    //stream.on("finish",  () => { resolve(true); }); 
    //stream.on("error", reject); 
}

function filewalker(dir, done) {
  let results = [];

  fs.readdir(dir, function (err, list) {
    if (err) return done(err);

    var pending = list.length;

    if (!pending) return done(null, results);

    list.forEach(function (file) {
      file = path.resolve(dir, file);

      fs.stat(file, function (err, stat) {
        // If directory, execute a recursive call
        if (stat && stat.isDirectory()) {

          filewalker(file, function (err, res) {
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

function generate_manifest(testinfo) {
  return new Promise((resolve, reject) => {
    const filepath = path.join(testinfo.serverDir, testinfo.versionName + ".sha256")
    fse.outputFileSync(filepath);
    
    filewalker(testinfo.serverDir, function (err, data) {
      if (err) {
        throw err;
      }

      var stream = fs.createWriteStream(filepath);

      for (var foundfile of data) {
        if (filepath == foundfile) {
          continue;
        } else {
          const hash = crypto.createHash('sha256');
          const input = fs.readFileSync(foundfile)

          hash.update(input);
          var update_subdirpath = path.join(testinfo.serverDir, testinfo.versionName)
          stream.write(`${hash.digest('hex')}` + " " + foundfile.substring(update_subdirpath.length) + "\n");

          const gzip = zlib.createGzip();
          const inp = fs.createReadStream(foundfile);
          const out = fs.createWriteStream(foundfile + ".gz");

          inp.pipe(gzip).pipe(out);

        }
      }
      if (testinfo.manifestWrongFile) {

        stream.write(`e980fe14384b38340fad866a92f2cbe4aeef268fac3368274bcb0b8e2cd32702` + " \\missing_file5.1\n");
      }
      stream.end();
      stream.on("finish", () => { resolve(true); }); // not sure why you want to pass a boolean
      stream.on("error", reject); // don't forget this!

      //console.log(data);
    });

  });
}

async function generate_server_dir(testinfo) {
  const update_subdirpath = path.join(testinfo.serverDir, testinfo.versionName)
  await generate_file(update_subdirpath, "test2.txt", "new change")
  await generate_file(update_subdirpath, "file2.jpeg")
  await generate_file(update_subdirpath, "file4.log.txt")
  await generate_file(update_subdirpath, "file5.1")
  await generate_file(update_subdirpath, "file 1.txt")
  await generate_file(update_subdirpath, "dir\\file6.ept", "", true)

  if(testinfo.morebigfiles)
  {
    let file_index;
    for (file_index = 0; file_index < morefilesmax; file_index++) 
    {
      let file_name = "dir_bigs\\file"+file_index+".txt";
      await generate_file(update_subdirpath, file_name, "", false, true)
    }
  }

  if (testinfo.manifestGenerated) {
    await generate_manifest(testinfo)
  }

  console.log("finish generate_server_dir");
}

async function generate_initial_dir(testinfo, dirpath = "") {
  if (dirpath == "") {
    dirpath = testinfo.initialDir
  }

  await generate_file(dirpath, "filea.exe", "", true)
  await generate_file(dirpath, "file1.exe")
  await generate_file(dirpath, "file2.txt")
  await generate_file(dirpath, "file2.jpeg")
  await generate_file(dirpath, "test2.txt", "old content", false)
  await generate_file(dirpath, "dir\\file3.zip")

  console.log("finish generate_initial_dir");
}

async function generate_result_dir(testinfo, dirpath) {
  await generate_file(dirpath, "filea.exe", "", true)
  await generate_file(dirpath, "file1.exe")
  await generate_file(dirpath, "file2.txt")
  await generate_file(dirpath, "file2.jpeg")
  await generate_file(dirpath, "test2.txt", "new change")
  await generate_file(dirpath, "file4.log.txt")
  await generate_file(dirpath, "file5.1")
  await generate_file(dirpath, "file 1.txt")
  await generate_file(dirpath, "dir\\file3.zip")
  await generate_file(dirpath, "dir\\file6.ept", "", true)

  if(testinfo.morebigfiles)
  {
    let file_index;
    for (file_index = 0; file_index < morefilesmax; file_index++) 
    {
      let file_name = "dir_bigs\\file"+file_index+".txt";
      await generate_file(dirpath, file_name, "", false, true)
    }
  }

  console.log("finish generate_result_dir");
}

clean_test_dir = function (dirpath) {
  try {
    fse.removeSync(dirpath);
  }
  catch (error) {

  }
  console.log("finish clean_dir for " + dirpath);
}

exports.clean_test_dirs = function (testinfo) {
  clean_test_dir(testinfo.serverDir);
  clean_test_dir(testinfo.initialDir);
  clean_test_dir(testinfo.resultDir);
  clean_test_dir(testinfo.reporterDir);
}

exports.generate_test_files = async function (testinfo) {
  exports.clean_test_dirs(testinfo);

  await generate_server_dir(testinfo);
  await generate_initial_dir(testinfo);

  if (testinfo.expectedResult == "filesupdated")
    await generate_result_dir(testinfo, testinfo.resultDir);
  if (testinfo.expectedResult == "filesnotchanged")
    await   generate_initial_dir(testinfo, testinfo.resultDir);

}

exports.check_results = function (testinfo) {
  var dircompare = require('dir-compare');
  var format = require('util').format;

  var options = { compareSize: true };
  var path1 = testinfo.initialDir;
  var path2 = testinfo.resultDir;

  var states = { 'equal': '==', 'left': '->', 'right': '<-', 'distinct': '<>' };

  var res = dircompare.compareSync(path1, path2, options);
  let ret = res.distinct != 0 && res.left != 0 || res.right != 0 || res.differences != 0

  console.log(format('Compare dirs: equal=%s, distinct=%s, left=%s, right=%s, differences=%s, same=%s',
    res.equal, res.distinct, res.left, res.right, res.differences, res.same));

  res.diffSet.forEach(function (entry) {
    var state = states[entry.state];
    var name1 = entry.name1 ? entry.name1 : '';
    var name2 = entry.name2 ? entry.name2 : '';

    if (ret) {
      console.log(format('%s(%s)%s%s(%s)', name1, entry.type1, state, name2, entry.type2));
    }
  });

  if (ret) {
    const report_path = path.join(testinfo.reporterDir, "crash_report.json")
    const have_report = fs.existsSync(report_path);
    
    if ((testinfo.expectedCrashReport && have_report ) 
    || (!testinfo.expectedCrashReport && !have_report )) {
      ret = true;
    } else {
      ret = false;
    }
  }

  return !ret;

}
