const run_test = require('./run_test.js');
const test_config = require('./test_config.js');
const run_one_test = false;

async function run_tests() {
    let testinfo
    let failed_tests = 0;
    if(run_one_test) {
        //single test for manual use 
        testinfo = test_config.gettestinfo();
        testinfo.let_404 = true;
        testinfo.let_block_one_file = true;
        testinfo.morebigfiles = true;
        testinfo.runAsInteractive = 1;
        failed_tests = failed_tests + await run_test.test_update(testinfo);
    } else {
        //good update
        testinfo = test_config.gettestinfo()
        failed_tests = failed_tests + await run_test.test_update(testinfo)

        //good huge files 
        testinfo = test_config.gettestinfo();
        testinfo.morebigfiles = true;
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //test recovering server error responce 
        testinfo = test_config.gettestinfo();
        testinfo.let_404 = true;
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //test closed server connection
        testinfo = test_config.gettestinfo();
        testinfo.let_drop = true;
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //test wrong content
        testinfo = test_config.gettestinfo();
        testinfo.let_wrong_header = true;
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //test server slow to responce connection
        testinfo = test_config.gettestinfo();
        testinfo.let_5sec = true;
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //test server more slow to responce connection
        testinfo = test_config.gettestinfo();
        testinfo.let_15sec = true;
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //test that update not continue after one file totaly failed 
        testinfo = test_config.gettestinfo();
        testinfo.let_404 = true;
        testinfo.let_block_one_file = true;
        testinfo.morebigfiles = true;
        testinfo.expectedResult = "filesnotchanged"
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //bad update - no server. files have to not change 
        testinfo = test_config.gettestinfo()
        testinfo.expectedResult = "filesnotchanged"
        testinfo.serverStarted = false
        failed_tests = failed_tests + await run_test.test_update(testinfo)

        //bad update - no manifest on server. files have to not change 
        testinfo = test_config.gettestinfo()
        testinfo.expectedResult = "filesnotchanged"
        testinfo.manifestGenerated = false
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        //bad update - wrong file in manifest. files have to not change 
        testinfo = test_config.gettestinfo()
        testinfo.expectedResult = "filesnotchanged"
        testinfo.manifestWrongFile = true
        failed_tests = failed_tests + await run_test.test_update(testinfo);

        if (false) {
            //wrong file in manifest. files have to not change 
            testinfo = test_config.gettestinfo()
            testinfo.expectedResult = "filesnotchanged"
            testinfo.blockFileFromUpdating = true
            await run_test.test_update(testinfo);
        }
    }
    if (failed_tests == 0) {
        console.log("Total result: all test completed.");
    } else {
        console.log("Total result: " + failed_tests + " tests failed.");
    }
}

(async () => {
    await run_tests();
})();