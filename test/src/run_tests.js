const run_test = require('./run_test.js');
const test_config = require('./test_config.js');

async function run_tests() {
    let testinfo
    let failed_tests = 0;
    
/*    //good update
    testinfo = test_config.gettestinfo()
    failed_tests = failed_tests + await run_test.test_update(testinfo)

    //good huge files 
    testinfo = test_config.gettestinfo();
    testinfo.morebigfiles = true;
    failed_tests = failed_tests + await run_test.test_update(testinfo);

    //test recovering server error responce 
    testinfo = test_config.gettestinfo();
    testinfo.morebigfiles = true;
    testinfo.let_404 = true;
    failed_tests = failed_tests + await run_test.test_update(testinfo);

    //test closed server connection
    testinfo = test_config.gettestinfo();
    testinfo.morebigfiles = true;
    testinfo.let_drop = true;
    failed_tests = failed_tests + await run_test.test_update(testinfo);

    //test closed server connection
    testinfo = test_config.gettestinfo();
    testinfo.morebigfiles = true;
    testinfo.let_wrong_header = true;
    failed_tests = failed_tests + await run_test.test_update(testinfo);
*/

    //test closed server connection
    testinfo = test_config.gettestinfo();
    testinfo.morebigfiles = true;
    testinfo.runAsInteractive = '1';
    testinfo.let_5sec = true;
    failed_tests = failed_tests + await run_test.test_update(testinfo);
/*
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
  */  
    if ( failed_tests == 0)
    {
        console.log("Total result: all test completed.");
    } else {
        console.log("Total result: " + failed_tests + " tests failed.");
    }
}

(async () => {
    await run_tests();
})();