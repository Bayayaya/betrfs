/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
/*
COPYING CONDITIONS NOTICE:

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation, and provided that the
  following conditions are met:

      * Redistributions of source code must retain this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below).

      * Redistributions in binary form must reproduce this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below) in the documentation and/or other materials
        provided with the distribution.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.

COPYRIGHT NOTICE:

  TokuDB, Tokutek Fractal Tree Indexing Library.
  Copyright (C) 2007-2013 Tokutek, Inc.

DISCLAIMER:

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

UNIVERSITY PATENT NOTICE:

  The technology is licensed by the Massachusetts Institute of
  Technology, Rutgers State University of New Jersey, and the Research
  Foundation of State University of New York at Stony Brook under
  United States of America Serial No. 11/760379 and to the patents
  and/or patent applications resulting from it.

PATENT MARKING NOTICE:

  This software is covered by US Patent No. 8,185,551.
  This software is covered by US Patent No. 8,489,638.

PATENT RIGHTS GRANT:

  "THIS IMPLEMENTATION" means the copyrightable works distributed by
  Tokutek as part of the Fractal Tree project.

  "PATENT CLAIMS" means the claims of patents that are owned or
  licensable by Tokutek, both currently or in the future; and that in
  the absence of this license would be infringed by THIS
  IMPLEMENTATION or by using or running THIS IMPLEMENTATION.

  "PATENT CHALLENGE" shall mean a challenge to the validity,
  patentability, enforceability and/or non-infringement of any of the
  PATENT CLAIMS or otherwise opposing any of the PATENT CLAIMS.

  Tokutek hereby grants to you, for the term and geographical scope of
  the PATENT CLAIMS, a non-exclusive, no-charge, royalty-free,
  irrevocable (except as stated in this section) patent license to
  make, have made, use, offer to sell, sell, import, transfer, and
  otherwise run, modify, and propagate the contents of THIS
  IMPLEMENTATION, where such license applies only to the PATENT
  CLAIMS.  This grant does not include claims that would be infringed
  only as a consequence of further modifications of THIS
  IMPLEMENTATION.  If you or your agent or licensee institute or order
  or agree to the institution of patent litigation against any entity
  (including a cross-claim or counterclaim in a lawsuit) alleging that
  THIS IMPLEMENTATION constitutes direct or contributory patent
  infringement, or inducement of patent infringement, then any rights
  granted to you under this License shall terminate as of the date
  such litigation is filed.  If you or your agent or exclusive
  licensee institute or order or agree to the institution of a PATENT
  CHALLENGE, then Tokutek may terminate any rights granted to you
  under this License.
*/

#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#ident "$Id$"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>

#include <toku_pthread.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <db.h>

#include "threaded_stress_test_helpers.h"

//
// This test is a form of stress that does operations on a single dictionary:
// We create a dictionary bigger than the cachetable (around 4x greater).
// Then, we spawn a bunch of pthreads that do the following:
//  - scan dictionary forward with bulk fetch
//  - scan dictionary forward slowly
//  - scan dictionary backward with bulk fetch
//  - scan dictionary backward slowly
//  - update existing values in the dictionary with db->put(DB_YESOVERWRITE)
//  - do random point queries into the dictionary
// With the small cachetable, this should produce quite a bit of churn in reading in and evicting nodes.
// If the test runs to completion without crashing, we consider it a success.
//
// This test differs from stress1 in that it grows the database through
// update operations.
//

static int remove_and_recreate_me(DB_TXN *UU(txn), ARG arg, void* UU(operation_extra), void *UU(stats_extra)) {
    int r;
    int db_index = myrandom_r(arg->random_data)%arg->cli->num_DBs;
    DB* db = arg->dbp[db_index];
    r = (db)->close(db, 0); CKERR(r);

    char name[30];
    ZERO_ARRAY(name);
    get_ith_table_name(name, sizeof(name), db_index);

    r = arg->env->dbremove(arg->env, null_txn, name, nullptr, 0);
    CKERR(r);

    r = db_create(&(arg->dbp[db_index]), arg->env, 0);
    assert(r == 0);
    // TODO: Need to call before_db_open_hook() and after_db_open_hook()
    r = arg->dbp[db_index]->open(arg->dbp[db_index], null_txn, name, nullptr, DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);
    return 0;
}

static void
stress_table(DB_ENV *env, DB **dbp, struct cli_args *cli_args) {
    //
    // the threads that we want:
    //   - one thread constantly updating random values
    //   - one thread doing table scan with bulk fetch
    //   - one thread doing table scan without bulk fetch
    //   - one thread doing random point queries
    //

    if (verbose) printf("starting creation of pthreads\n");
    const int num_threads = 5 + cli_args->num_update_threads + cli_args->num_ptquery_threads;
    struct arg myargs[num_threads];
    for (int i = 0; i < num_threads; i++) {
        arg_init(&myargs[i], dbp, env, cli_args);
    }

    struct scan_op_extra soe[4];

    // make the forward fast scanner
    soe[0].fast = true;
    soe[0].fwd = true;
    soe[0].prefetch = false;
    myargs[0].lock_type = STRESS_LOCK_SHARED;
    myargs[0].operation_extra = &soe[0];
    myargs[0].operation = scan_op;

    // make the forward slow scanner
    soe[1].fast = false;
    soe[1].fwd = true;
    soe[1].prefetch = false;
    myargs[1].lock_type = STRESS_LOCK_SHARED;
    myargs[1].operation_extra = &soe[1];
    myargs[1].operation = scan_op;

    // make the backward fast scanner
    soe[2].fast = true;
    soe[2].fwd = false;
    soe[2].prefetch = false;
    myargs[2].lock_type = STRESS_LOCK_SHARED;
    myargs[2].operation_extra = &soe[2];
    myargs[2].operation = scan_op;

    // make the backward slow scanner
    soe[3].fast = false;
    soe[3].fwd = false;
    soe[3].prefetch = false;
    myargs[3].lock_type = STRESS_LOCK_SHARED;
    myargs[3].operation_extra = &soe[3];
    myargs[3].operation = scan_op;

    // make the guy that removes and recreates the db
    myargs[4].lock_type = STRESS_LOCK_EXCL;
    myargs[4].sleep_ms = 2000; // maybe make this a runtime param at some point
    myargs[4].operation = remove_and_recreate_me;

    // make the guy that updates the db
    struct update_op_args uoe = get_update_op_args(cli_args, NULL);
    for (int i = 5; i < 5 + cli_args->num_update_threads; ++i) {
        myargs[i].bounded_element_range = false;
        myargs[i].lock_type = STRESS_LOCK_SHARED;
        myargs[i].operation_extra = &uoe;
        myargs[i].operation = update_op;
    }

    // make the guy that does point queries
    for (int i = 5 + cli_args->num_update_threads; i < num_threads; i++) {
        myargs[i].lock_type = STRESS_LOCK_SHARED;
        myargs[i].bounded_element_range = false;
        myargs[i].operation = ptquery_op_no_check;
    }

    run_workers(myargs, num_threads, cli_args->num_seconds, false, cli_args);
}

extern "C" int test_test_stress6(void);
int test_test_stress6(void) {
    pre_setup();
    struct cli_args args = get_default_args();
    args.num_elements = 150000;
    args.num_seconds = 1000;
    args.join_timeout = 600;
    stress_test_main(&args);
    post_teardown();
    return 0;
}
