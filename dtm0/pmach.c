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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM0
#include "lib/trace.h"

#include "dtm0/pmach.h"
#include "dtm0/co_fom.h"     /* m0_co_fom */
#include "dtm0/net.h"        /* m0_dtm0_net */
#include "dtm0/service.h"    /* m0_dtm0_is_a_persistent_dtm */
#include "be/dtm0_log.h"     /* m0_be_dtm0_log_credit */
#include "reqh/reqh.h"       /* m0_reqh */
#include "fop/fom_generic.h" /* M0_FOPH_INIT */
#include "lib/memory.h"      /* M0_ALLOC_PTR */
#include "rpc/rpc_opcodes.h" /* M0_DTM0_PMSG_OPCODE */

struct pmsg_fom {
	struct m0_fom                 pf_base;
	struct m0_fid                 pf_where;
	struct m0_dtm0_tx_desc        pf_msg;
	struct m0_dtm0_pmsg_ast      *pf_pma;
	struct m0_be_op              *pf_op;
};

struct m0_fom *pmsg_fom2base(struct pmsg_fom *fom)
{
	return &fom->pf_base;
}

struct pmsg_fom *base2pmsg_fom(struct m0_fom *fom)
{
	/* TODO: bob_of */
	return (struct pmsg_fom *) fom;
}

static void calculate_credits(struct m0_fom *base)
{
	struct pmsg_fom        *fom = base2pmsg_fom(base);
	struct m0_dtm0_tx_desc *txd = &fom->pf_msg;
	struct m0_buf           buf = {};
	struct m0_be_tx_credit  cred = {};

	if (m0_dtm0_is_a_persistent_dtm(base->fo_service)) {
		m0_be_dtm0_log_credit(M0_DTML_PERSISTENT,
				      txd, &buf,
				      m0_fom_reqh(base)->rh_beseg,
				      NULL, &cred);
		m0_be_tx_credit_add(&base->fo_tx.tx_betx_cred, &cred);
	}
}

static void update_log(struct m0_fom *base)
{
	struct pmsg_fom               *fom = base2pmsg_fom(base);
	struct m0_dtm0_tx_desc        *txd = &fom->pf_msg;
	struct m0_dtm0_service        *svc = m0_dtm0_fom2service(base);
	struct m0_be_dtm0_log         *log = svc->dos_log;
	struct m0_buf                  buf = {};
	int                            rc;

	M0_ASSERT(svc != NULL);
	M0_ENTRY("fom=%p", base);

	if (m0_dtm0_is_a_volatile_dtm(base->fo_service)) {
		/*
		 * On the client side, DTX is the owner of the
		 * corresponding log record, so that it cannot be
		 * modifed right here. We have to post an AST
		 * to ensure DTX is modifed under the group lock held.
		 */
		M0_ASSERT(false);
		m0_be_dtm0_log_pmsg_post(svc->dos_log,
					 (struct m0_fop *) fom->pf_pma);
	} else {
		m0_mutex_lock(&log->dl_lock);
		rc = m0_be_dtm0_log_update(svc->dos_log, &base->fo_tx.tx_betx,
					   txd, &buf);
		M0_ASSERT_INFO(rc == 0, "Failed to update persistent log?");
		m0_mutex_unlock(&log->dl_lock);
	}
	M0_LEAVE();
}

static int pmsg_fom_tick(struct m0_fom *fom)
{
	int outcome = M0_FSO_AGAIN;
	int phase   = m0_fom_phase(fom);

	M0_ENTRY("fom %p phase %d", fom, phase);

	switch (phase) {
	case M0_FOPH_INIT ... M0_FOPH_NR - 1:
		outcome = m0_fom_tick_generic(fom);
		if (m0_fom_phase(fom) == M0_FOPH_TXN_OPEN) {
			M0_ASSERT(phase == M0_FOPH_TXN_INIT);
			calculate_credits(fom);
		}
		break;
	case M0_FOPH_TYPE_SPECIFIC:
		update_log(fom);
		outcome = M0_FSO_AGAIN;
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		break;
	default:
		M0_IMPOSSIBLE("Invalid phase");
	}

	return M0_RC(outcome);
}

static void pmsg_fom_fini(struct m0_fom *base)
{
	M0_ASSERT(false);
}

static size_t pmsg_fom_locality(const struct m0_fom *fom)
{
	static size_t locality = 0;
	M0_PRE(fom != NULL);
	return locality++;
}


static const struct m0_fom_ops pmsg_fom_ops = {
	.fo_fini          = pmsg_fom_fini,
	.fo_tick          = pmsg_fom_tick,
	.fo_home_locality = pmsg_fom_locality
};

static struct m0_fom_type pmsg_fom_type;

static struct pmsg_fom *pmsg_fom_alloc(struct m0_reqh *reqh,
				       struct m0_dtm0_tx_desc *txd)
{
	struct pmsg_fom         *fom = NULL;
	struct m0_dtm0_pmsg_ast *pma = NULL;
	int                      rc;

	M0_ENTRY("reqh=%p", reqh);

	M0_ALLOC_PTR(fom);
	M0_ALLOC_PTR(pma);

	if (fom == NULL || pma == NULL) {
		m0_free(fom);
		m0_free(pma);
		return NULL;
	}

	rc = m0_dtm0_tx_desc_copy(txd, &pma->p_txd) ?:
		m0_dtm0_tx_desc_copy(txd, &fom->pf_msg);
	if (rc != 0) {
		m0_dtm0_tx_desc_fini(&pma->p_txd);
		m0_dtm0_tx_desc_fini(&fom->pf_msg);
		m0_free(pma);
		m0_free(fom);
		return NULL;
	}

	fom->pf_pma = pma;
	m0_fom_init(&fom->pf_base, &pmsg_fom_type,
		    &pmsg_fom_ops, NULL, NULL, reqh);

	M0_LEAVE("fom=%p", fom);
	return fom;
}

static void pmsg_fom_free(struct pmsg_fom *fom)
{
	m0_free(fom);
}

static const struct m0_fom_type_ops pmsg_fom_type_ops = {
	.fto_create = NULL
};

/* TODO */
extern struct m0_reqh_service_type m0_cfs_stype;

static void pmsg_fom_launch(struct m0_reqh         *reqh,
			    struct m0_be_op        *op,
			    struct m0_dtm0_tx_desc *txd)
{
	struct pmsg_fom *fom;

	M0_ENTRY();

	fom = pmsg_fom_alloc(reqh, txd);

	if (fom != NULL)
		m0_fom_queue(&fom->pf_base);
	else
		pmsg_fom_free(fom);

	M0_LEAVE();
}


#define F M0_CO_FRAME_DATA

static void pmach_remote_tick(struct m0_co_fom *cf,
			      void             *cf_arg)
{
	struct m0_dtm0_pmach   *pmach = cf_arg;
	struct m0_dtm0_net     *dnet = pmach->dpm_cfg.dpmc_net;
	struct m0_dtm0_tx_desc *txd = NULL;
	struct m0_reqh         *reqh = cf->cf_base.fo_service->rs_reqh;

	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),
		      bool               success;
		      struct m0_dtm0_msg msg;);

	do {
		m0_dtm0_net_recv(dnet, &cf->cf_op, &F(success),
				 &F(msg), M0_DMT_PERSISTENT);
		M0_CO_FOM_AWAIT(cf);
		if (F(success)) {
			txd = &F(msg).dm_msg.persistent.dmp_txd;
			pmsg_fom_launch(reqh, &cf->cf_op, txd);
			M0_CO_FOM_AWAIT(cf);
		} else
			break;
	} while (true);
}

static bool next_target(int *index,
			struct m0_fid *target,
			const struct m0_dtm0_tx_desc *txd,
			const struct m0_fid *local)
{
	int i = *index;

	if (i == txd->dtd_ps.dtp_nr)
		return false;

	*target = txd->dtd_ps.dtp_pa[i].p_fid;

	/* Select the originator instead of outselves. */
	if (m0_fid_eq(local, target))
		*target = txd->dtd_id.dti_fid;

	*index = i + 1;
	return true;
}

static int m0_dtm0_tid_array_init(struct m0_dtm0_tid_array *ta, int nr)
{
	M0_PRE(M0_IS0(ta));
	M0_ALLOC_ARR(ta->buf, nr);
	if (ta->buf == NULL)
		return M0_ERR(-ENOMEM);
	ta->size = nr;
	return 0;
}

static void m0_dtm0_tid_array_fini(struct m0_dtm0_tid_array *ta)
{
	m0_free(ta);
	M0_SET0(ta);
}

static int pmsg_init(struct m0_dtm0_msg *msg, int nr,
		     const struct m0_fid *initiator)
{
	M0_PRE(M0_IS0(msg));
	msg->dm_type = M0_DMT_PERSISTENT;
	msg->dm_msg.persistent.dmp_initiator = *initiator;
	return m0_dtm0_tid_array_init(&msg->dm_msg.persistent.dmp_tid_array,
				      nr);
}

static void pmsg_fini(struct m0_dtm0_msg *msg)
{
	m0_dtm0_tid_array_fini(&msg->dm_msg.persistent.dmp_tid_array);
	M0_SET0(msg);
}

static void pmach_local_tick(struct m0_co_fom *cf, void *cf_arg)
{
	struct m0_dtm0_pmach *pmach = cf_arg;
	struct m0_dtm0_net   *dnet  = pmach->dpm_cfg.dpmc_net;
	int                   rc;

	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),
		      bool               success;
		      struct m0_fid      local;
		      struct m0_fid      target;
		      struct m0_dtm0_msg msg;
		      struct m0_dtm0_tx_desc txd;
		      int                index;);

	F(msg) = (struct m0_dtm0_msg) {};
	F(local) = pmach->dpm_cfg.dpmc_initiator;
	F(success) = true;
	F(txd) = (struct m0_dtm0_tx_desc) {};
	F(index) = 0;

	rc = pmsg_init(&F(msg), 1, &F(local));
	if (rc != 0) {
		m0_co_fom_failed(cf, M0_ERR(rc));
		return;
	}

	do {
		m0_be_queue_lock(pmach->dpm_local_q);
		M0_BE_QUEUE_GET(pmach->dpm_local_q, &cf->cf_op,
				&F(txd), &F(success));
		m0_be_queue_unlock(pmach->dpm_local_q);
		M0_CO_FOM_AWAIT(cf);
		F(msg).dm_msg.persistent.dmp_txd = F(txd);
		if (F(success)) {
			while (next_target(&F(index), &F(target),
					   &F(txd), &F(local))) {
				m0_dtm0_net_send(dnet, &cf->cf_op, &F(target),
						 &F(msg), NULL);
				M0_CO_FOM_AWAIT(cf);
			}
		} else
			break;
	} while (true);

	pmsg_fini(&F(msg));
}

M0_INTERNAL int m0_dtm0_pmach_init(struct m0_dtm0_pmach     *dpm,
                                   struct m0_dtm0_pmach_cfg *dpm_cfg)
{
	int                    rc;
	struct m0_be_queue_cfg q_cfg = {
		.bqc_q_size_max = 10,
		.bqc_producers_nr_max = 1,
		.bqc_consumers_nr_max = 1,
		.bqc_item_length = sizeof(struct m0_dtm0_tx_desc),
	};

	struct m0_co_fom_cfg   cf_cfg_local = {
		.cfc_tick = pmach_local_tick,
		.cfc_arg  = dpm,
		.cfc_reqh = dpm_cfg->dpmc_reqh,
	};

	struct m0_co_fom_cfg   cf_cfg_remote = {
		.cfc_tick = pmach_remote_tick,
		.cfc_arg  = dpm,
		.cfc_reqh = dpm_cfg->dpmc_reqh,
	};

	M0_PRE(dpm_cfg->dpmc_net != NULL);
	dpm->dpm_cfg = *dpm_cfg;

	M0_ALLOC_PTR(dpm->dpm_local);
	M0_ALLOC_PTR(dpm->dpm_remote);

	if (dpm->dpm_local == NULL ||
	    dpm->dpm_remote == NULL)
		return M0_ERR(-ENOMEM);

	m0_fom_type_init(&pmsg_fom_type, M0_DTM0_PMSG_OPCODE,
			 &pmsg_fom_type_ops, &m0_cfs_stype,
			 &m0_generic_conf);

	M0_ALLOC_PTR(dpm->dpm_local_q);
	if (dpm->dpm_local_q == NULL)
		return M0_ERR(-ENOMEM);

	rc = m0_be_queue_init(dpm->dpm_local_q, &q_cfg);
	if (rc != 0)
		return M0_ERR(rc);

	return m0_co_fom_init(dpm->dpm_remote, &cf_cfg_remote) ?:
		m0_co_fom_init(dpm->dpm_local, &cf_cfg_local);
}

M0_INTERNAL void m0_dtm0_pmach_txd_post(struct m0_dtm0_pmach *dpm,
					struct m0_be_op      *op,
					const struct m0_dtm0_tx_desc *txd)
{
	m0_be_queue_lock(dpm->dpm_local_q);
	M0_BE_QUEUE_PUT(dpm->dpm_local_q, op, txd);
	m0_be_queue_unlock(dpm->dpm_local_q);
}

M0_INTERNAL void m0_dtm0_pmach_fini(struct m0_dtm0_pmach  *dpm)
{
	m0_be_queue_fini(dpm->dpm_local_q);
	m0_free0(&dpm->dpm_local_q);
	/* TODO */
}

M0_INTERNAL void m0_dtm0_pmach_start(struct m0_dtm0_pmach *dpm)
{
	m0_co_fom_start(dpm->dpm_remote);
	m0_co_fom_start(dpm->dpm_local);
}

M0_INTERNAL void m0_dtm0_pmach_stop(struct m0_dtm0_pmach  *dpm)
{
	m0_be_queue_lock(dpm->dpm_local_q);
	m0_be_queue_end(dpm->dpm_local_q);
	m0_be_queue_unlock(dpm->dpm_local_q);
}

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
