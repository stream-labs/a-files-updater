const run_test = require('./run_test.js');
const test_config = require('./test_config.js');

async function run_tests() {
    let testinfo
    //good 
    testinfo = test_config.gettestinfo()
    testinfo.morebigfiles = true;
    testinfo.skipUpdaterLaunch = true;
    testinfo.manifestGenerated = true;
    await run_test.test_update(testinfo)
}

(async () => {
    await run_tests();
})();