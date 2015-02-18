#include "test.h"

#include <stdio.h>
#include <stdlib.h>

#include <toku_pthread.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <db.h>

#include "threaded_stress_test_helpers.h"



// The intent of this test is to measure the throughput of db->puts
// with multiple threads.


static void
stress_table(DB_ENV* env, DB** dbp, struct cli_args *cli_args) {
    if (verbose) printf("starting creation of pthreads\n");
    const int num_threads = cli_args->num_put_threads;
    struct arg myargs[num_threads];
    operation_t put_op = (cli_args->serial_insert
                          ? serial_put_op
                          : random_put_op_singledb);
    struct serial_put_extra spe[num_threads];
    ZERO_ARRAY(spe);
    for (int i = 0; i < num_threads; i++) {
        arg_init(&myargs[i], dbp, env, cli_args);
        myargs[i].operation = put_op;
        if (cli_args->serial_insert) {
            spe[i].current = cli_args->num_elements;
            myargs[i].operation_extra = &spe[i];
        }
    }
    const bool crash_at_end = false;
    run_workers(myargs, num_threads, cli_args->num_seconds, 
                crash_at_end, cli_args);
}

extern "C" int bench_insert(void);
int bench_insert(void) {
    pre_setup();
    struct cli_args args = get_default_args_for_perf();
    args.num_elements = 0;  // want to start with empty DBs
    args.key_size = 8;
    args.val_size = 8;
    args.print_performance = true;
    args.num_elements = 1000; //default of 1M
  
    // when there are multiple threads, its valid for two of them to
    // generate the same key and one of them fail with DB_LOCK_NOTGRANTED
    if (args.num_put_threads > 1) {
        args.crash_on_operation_failure = false;
    }    
    perf_test_main(&args);
    post_teardown();
    return 0;
}
