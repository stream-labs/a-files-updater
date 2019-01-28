const run_test = require('./run_test.js');
const test_config = require('./test_config.js');

async function run_tests() {
    let testinfo 
    //good 
    testinfo = test_config.gettestinfo()
    await run_test.test_update(testinfo)

    //no server. files have to not change 
    testinfo = test_config.gettestinfo()
    testinfo.expectedResult = "filesnotchanged"
    testinfo.serverStarted = false
    await run_test.test_update(testinfo)

    //wrong file in manifest. files have to not change 
    testinfo = test_config.gettestinfo()
    testinfo.expectedResult = "filesnotchanged"
    testinfo.manifestWrongFile = true
    await run_test.test_update(testinfo);
}

(async() => {
    await run_tests();
} )();