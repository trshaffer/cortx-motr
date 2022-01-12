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
#include "dtm0/co_fom.h" /* m0_co_fom */
#include "dtm0/net.h"    /* m0_dtm0_net */

#if 0
#define F M0_CO_FRAME_DATA

M0_INTERNAL void m0_dtm0_log_pmsg_post(struct m0_dtm0_log  *dol,
				       struct m0_be_op     *op,
				       const struct m0_fid *where,
				       struct m0_dtm0_tid  *tid,
				       uint64_t             tid_nr);

static void pmach_remote_tick(struct m0_co_fom *cf,
			      void             *cf_arg)
{
	struct m0_dtm0_pmach *pmach = cf_arg;
	struct m0_dtm0_net   *dnet = pmach->dpm_net;
	struct m0_dtm0_log   *dol  = pmach->dpm_dol;

	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),
		      bool               success;
		      struct m0_dtm0_msg msg;);

	while (F(success)) {
		m0_dtm0_net_recv(dnet, &cf->cf_op, &F(success),
				 &F(msg), M0_DMT_PERSISTENT);
		M0_CO_FOM_AWAIT(cf);
		if (F(success)) {
			tids = F(msg)->dm_msg.persistent.dmp_tid_array;
			m0_dtm0_log_pmsg_post(dol,

					      &F(msg), &cf->cf_op);
			M0_CO_FOM_AWAIT(cf);
		}
	}
}

static bool next_target(int *index,
			struct m0_fid *target,
			struct m0_dtm0_tid *tid,
			const struct m0_dtm0_tx_desc *txd,
			const struct m0_fid *local)
{
	int i = *index;


	if (i == txd->dtd_ps.dtp_nr)
		return false;

	*target = txd->dtd_ps.dtp_pa[i].p_fid;

	/* Select the originator instead outselves. */
	if (m0_fid_eq(local, *target)) {
		*target = txd->dtd_id.dti_fid;
	}

	return true;
}

static int m0_dtm0_tid_array_init(struct m0_dtm0_tid_array *ta, int nr)
{
	M0_PRE(M0_IS0(ta));
	M0_ALLOC_ARR(ta->buf, nr);
	ta->size = nr;
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
	msg->persitent.dmp_initiator = initiator;
	return m0_dtm0_tid_array_init(msg->persistent.dmp_tid_array, nr);
}

static void pmsg_fini(struct m0_dtm0_msg *msg)
{
	m0_dtm0_tid_array_fini(msg->persistent.dmp_tid_array);
	M0_SET0(msg);
}

static void pmach_local_tick(struct m0_co_fom *cf,
			    void             *cf_arg)
{
	struct m0_dtm0_pmach *pmach = cf_arg;
	struct m0_dtm0_net   *dnet = pmach->dpm_net;
	struct m0_dtm0_log   *dol  = pmach->dpm_dol;

	M0_CO_REENTER(M0_CO_FOM_CONTEXT(cf),
		      bool               success;
		      struct m0_fid      local;
		      struct m0_dtm0_msg msg;
		      struct m0_dtm0_tx_desc txd;);

	F(msg) = (struct m0_dtm0_msg) {};
	F(local) = local_fid(dnet);
	F(success) = true;
	F(txd) = (struct m0_dtm0_tx_desc) {};

	rc = pmsg_init(&F(msg), 1, &F(local));
	if (rc != 0)
		return M0_ERR(m0_co_fom_failed(rc));

	while (success) {
		m0_dtm0_log_pmsg_wait(dnet, &cf->cf_op, &F(success), &F(txd));
		M0_CO_FOM_AWAIT(cf);
		if (F(success)) {
			while (next_target(&F(index), &F(target),
					   F(msg->persistent.dmp_tid_array.buf),
					   &F(txd), &F(local))) {
				m0_dtm0_net_send(dnet, &cf->cf_op,
						 F(target), F(msg));
				M0_CO_FOM_AWAIT(cf);
			}
		}
	}

	pmsg_fini(&F(msg));
}

M0_INTERNAL int m0_dtm0_pmach_init(struct m0_dtm0_pmach     *dpm,
                                   struct m0_dtm0_pmach_cfg *dpm_cfg)
{
	struct m0_co_fom_cfg cf_cfg_local = {
		.cf_tick = pmach_local_tick,
		.cf_arg  = dpm,
	};

	struct m0_co_fom_cfg cf_cfg_remote = {
		.cf_tick = pmach_remote_tick,
		.cf_arg  = dpm,
	};

	M0_ALLOC_PTR(dpm->dpm_cf_local);
	M0_ALLOC_PTR(dpm->dpm_cf_remote);

	if (dpm->dpm_cf_local == NULL ||
	    dpm->dpm_cf_remote == NULL)
		return M0_ERR(-ENOMEM);

	return m0_co_fom_init(dpm->dpm_cf_local, &cf_cfg_remote) ?:
		m0_co_fom_init(dpm->dpm_cfg_local, &cf_cfg_local);
}
#else
M0_INTERNAL int m0_dtm0_pmach_init(struct m0_dtm0_pmach     *dpm,
                                   struct m0_dtm0_pmach_cfg *dpm_cfg)
{
	return 0;
}
#endif
M0_INTERNAL void m0_dtm0_pmach_fini(struct m0_dtm0_pmach  *drm)
{
}

M0_INTERNAL void m0_dtm0_pmach_start(struct m0_dtm0_pmach *drm)
{
}

M0_INTERNAL void m0_dtm0_pmach_stop(struct m0_dtm0_pmach  *drm)
{
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
