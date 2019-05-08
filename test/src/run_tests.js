const run_test = require('./run_test.js');
const test_config = require('./test_config.js');

const fse = require('fs-extra')
const path = require('path');

const run_one_test = false;

async function run_tests() {
    let testinfo;
    let failed_test_names = [];
    let test_result = 0;

    const testfilesDir = path.join(__dirname, "..", "testfiles");
    fse.removeSync(testfilesDir);

    if (run_one_test) {
        console.log("--- Tests launched in manual set of tests mode! ");
        //testinfo.let_404 = true;
        //testinfo.let_block_one_file = true;
        //testinfo.morebigfiles = true;
        //testinfo.expectedResult = "filesnotchanged"
        //testinfo.selfBlockingFile = true;
        //testinfo.selfLockingFile = true;
        //testinfo.runAsInteractive = 1;
        //testinfo.expectedResult = "filesnotchanged"
        //testinfo.manifestGenerated = false;
        testinfo = test_config.gettestinfo(" // test for manual use ");
        //testinfo.let_5sec = true;
        testinfo.runAsInteractive = 1;
        //testinfo.morebigfiles = true;
        testinfo.selfBlockersCount = 5;
        testinfo.selfBlockingFile = true;
        //testinfo.selfLockingFile = true;

        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

    } else {
        testinfo = test_config.gettestinfo(" //good update ");
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //good huge files  ");
        testinfo.morebigfiles = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test recovering server error responce  ");
        testinfo.let_404 = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test closed server connection ");
        testinfo.let_drop = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test wrong content ");
        testinfo.let_wrong_header = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test server slow to responce connection ");
        testinfo.let_5sec = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test server more slow to responce connection ");
        testinfo.let_15sec = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test that update not continue after one file totaly failed  ");
        testinfo.let_404 = true;
        testinfo.let_block_one_file = true;
        testinfo.morebigfiles = true;
        testinfo.expectedResult = "filesnotchanged"
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //bad update - no server. files have to not change  ");
        testinfo.expectedResult = "filesnotchanged"
        testinfo.serverStarted = false;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test some exe file blocked by rinnig it   ");
        testinfo.selfBlockingFile = true;
        testinfo.selfBlockersCount = 5;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //test some file blocked by win api  ");
        //testinfo.expectedResult = "filesnotchanged"
        testinfo.selfLockingFile = true;
        testinfo.selfBlockersCount = 2;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //bad update - no manifest on server. files have to not change  ");
        testinfo.expectedResult = "filesnotchanged"
        testinfo.manifestGenerated = false;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

        testinfo = test_config.gettestinfo(" //bad update - wrong file in manifest. files have to not change  ");
        testinfo.expectedResult = "filesnotchanged"
        testinfo.manifestWrongFile = true;
        test_result = await run_test.test_update(testinfo);
        if (test_result != 0) {
            failed_test_names.push(testinfo.testName);
        }

    }

    if (failed_test_names.length == 0) {
        console.log("=== Total result: all test completed.");
    } else {
        console.log("=== Total result: " + failed_test_names.length + " tests failed. \n Tests:" + failed_test_names.join("\n"));
    }
}

(async () => {
    await run_tests();
})();