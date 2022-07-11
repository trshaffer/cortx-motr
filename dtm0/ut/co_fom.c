/* -*- C -*- */
/*
 * Copyright (c) 2013-2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

/**
 * @addtogroup dtm0
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "dtm0/co_fom.h"
#include "lib/memory.h" /* M0_ALLOC_PTR */
#include "reqh/reqh.h"  /* m0_reqh */
#include "ut/ut.h"      /* M0_UT_ASSERT */
#include "reqh/reqh_service.h" /* m0_reqh_service_allocate */
#include "be/queue.h"   /* m0_be_queue */
#include "lib/types.h"  /* uint64_t */

#define F M0_CO_FRAME_DATA

extern struct m0_reqh_service_type dtm0_service_type;

static void init_fini_tick(struct m0_co_fom *cf, void *arg)
{
	(void) cf;
	(void) arg;
}

struct ut_ctx {
	struct m0_reqh         *reqh;
	struct m0_reqh_service *dtms;
};

static void ut_ctx_reqh_init(struct ut_ctx *uc)
{
	struct m0_reqh         *reqh = NULL;
	struct m0_reqh_service *dtms = NULL;
	int                     rc;

	M0_ALLOC_PTR(reqh);
	M0_UT_ASSERT(reqh != NULL);

	rc = M0_REQH_INIT(reqh,
			  .rhia_dtm       = NULL,
			  .rhia_db        = NULL,
			  .rhia_mdstore   = (void *) 1,
			  .rhia_fid       = &g_process_fid,
			 );
	M0_UT_ASSERT(rc == 0);

	rc = m0_reqh_service_allocate(&dtms, &dtm0_service_type, NULL);
	M0_UT_ASSERT(rc == 0);
	m0_reqh_service_init(dtms, reqh, NULL);
	rc = m0_reqh_service_start(dtms);
	M0_UT_ASSERT(rc == 0);

	uc->reqh = reqh;
	uc->dtms = dtms;
}

static void ut_ctx_reqh_fini(struct ut_ctx *uc)
{
	struct m0_reqh         *reqh = uc->reqh;
	struct m0_reqh_service *dtms = uc->dtms;
	m0_reqh_service_prepare_to_stop(dtms);
	m0_reqh_service_stop(dtms);
	m0_reqh_service_fini(dtms);
	m0_reqh_fini(reqh);
	M0_SET0(uc);
}

void m0_dtm0_ut_co_fom_init_fini(void)
{
	struct ut_ctx        uc;
	struct m0_co_fom    *cf;
	int                  rc;

	M0_ALLOC_PTR(cf);
	M0_UT_ASSERT(cf != NULL);

	ut_ctx_reqh_init(&uc);

	rc = m0_co_fom_init(cf, &(struct m0_co_fom_cfg) {
			    .cfc_reqh = uc.reqh,
			    .cfc_tick = init_fini_tick,
			    .cfc_arg  = NULL,
			    });
	M0_UT_ASSERT(rc == 0);
	m0_co_fom_fini(cf);
	m0_free(cf);

	ut_ctx_reqh_fini(&uc);
}

static void start_stop_tick(struct m0_co_fom *cf, void *arg)
{
	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),;);
}

void m0_dtm0_ut_co_fom_start_stop(void)
{
	struct ut_ctx        uc;
	struct m0_co_fom    *cf;
	int                  rc;
	struct m0_be_op      stop_op = {};

	M0_ALLOC_PTR(cf);
	M0_UT_ASSERT(cf != NULL);

	ut_ctx_reqh_init(&uc);

	m0_be_op_init(&stop_op);
	m0_be_op_active(&stop_op);
	rc = m0_co_fom_init(cf, &(struct m0_co_fom_cfg) {
			    .cfc_reqh = uc.reqh,
			    .cfc_tick = start_stop_tick,
			    .cfc_arg  = NULL,
			    .cfc_fini_op = &stop_op,
			    });
	M0_UT_ASSERT(rc == 0);

	m0_co_fom_start(cf);
	m0_be_op_wait(&stop_op);
	m0_be_op_fini(&stop_op);

	ut_ctx_reqh_fini(&uc);
	m0_free(cf);
}

enum {
	PROD_CONS_ITEM_NR = 100,
};

struct prod_cons {
	struct m0_be_queue q;
	struct m0_co_fom   prod;
	struct m0_be_op    prod_stopped;
	struct m0_co_fom   cons;
	struct m0_be_op    cons_stopped;

	uint64_t           limit;
	uint64_t           produced;
	uint64_t           consumed;
};

static void prod_tick(struct m0_co_fom *cf, void *arg)
{
	struct prod_cons *pc = arg;
	struct m0_be_queue *q = &pc->q;

	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),
		      uint64_t item;);

	for (F(item) = 0; F(item) < pc->limit; F(item)++) {
		m0_be_queue_lock(q);
		M0_BE_QUEUE_PUT(q, &cf->cf_op, &F(item));
		m0_be_queue_unlock(q);
		M0_CO_FOM_AWAIT(cf);
		pc->produced++;
	}

	m0_be_queue_lock(q);
	m0_be_queue_end(q);
	m0_be_queue_unlock(q);
}

static void cons_tick(struct m0_co_fom *cf, void *arg)
{
	struct prod_cons *pc = arg;
	struct m0_be_queue *q = &pc->q;

	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),
		      bool success;
		      uint64_t item;);

	F(success) = true;

	do {
		m0_be_queue_lock(q);
		M0_BE_QUEUE_GET(q, &cf->cf_op, &F(item), &F(success));
		m0_be_queue_unlock(q);
		M0_CO_FOM_AWAIT(cf);
		if (F(success))
			pc->consumed++;
	} while (F(success));
}

static void prod_cons_init(struct ut_ctx *uc, struct prod_cons *pc)
{
	int rc;

	M0_SET0(pc);

	pc->limit = PROD_CONS_ITEM_NR;

	rc = m0_be_queue_init(&pc->q,
			      &(struct m0_be_queue_cfg){
			      .bqc_q_size_max = 1,
			      .bqc_producers_nr_max = 1,
			      .bqc_consumers_nr_max = 1,
			      .bqc_item_length = sizeof(uint64_t),
			      });
	M0_UT_ASSERT(rc == 0);
	m0_be_op_init(&pc->prod_stopped);
	m0_be_op_init(&pc->cons_stopped);

	m0_be_op_active(&pc->prod_stopped);
	m0_be_op_active(&pc->cons_stopped);
	rc = m0_co_fom_init(&pc->prod, &(struct m0_co_fom_cfg) {
			    .cfc_reqh = uc->reqh,
			    .cfc_tick = prod_tick,
			    .cfc_arg  = pc,
			    .cfc_fini_op = &pc->prod_stopped,
			    });
	rc = m0_co_fom_init(&pc->cons, &(struct m0_co_fom_cfg) {
			    .cfc_reqh = uc->reqh,
			    .cfc_tick = cons_tick,
			    .cfc_arg  = pc,
			    .cfc_fini_op = &pc->cons_stopped,
			    });
	M0_UT_ASSERT(rc == 0);
}


static void prod_cons_fini(struct prod_cons *pc)
{
	m0_be_queue_fini(&pc->q);
	m0_be_op_fini(&pc->prod_stopped);
	m0_be_op_fini(&pc->cons_stopped);
}

static void prod_cons_start(struct prod_cons *pc)
{
	m0_co_fom_start(&pc->prod);
	m0_co_fom_start(&pc->cons);
}

static void prod_cons_wait(struct prod_cons *pc)
{
	m0_be_op_wait(&pc->prod_stopped);
	m0_be_op_wait(&pc->cons_stopped);
}

void m0_dtm0_ut_co_fom_prod_cons(void)
{
	struct ut_ctx        uc;
	struct prod_cons     pc;

	ut_ctx_reqh_init(&uc);
	prod_cons_init(&uc, &pc);
	prod_cons_start(&pc);
	prod_cons_wait(&pc);
	M0_UT_ASSERT(pc.produced == pc.limit);
	M0_UT_ASSERT(pc.consumed == pc.limit);
	prod_cons_fini(&pc);
	ut_ctx_reqh_fini(&uc);
}


#undef F
#undef M0_TRACE_SUBSYSTEM

/** @} end of dtm0 group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
