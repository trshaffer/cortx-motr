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
 * @addtogroup co_fom
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM0
#include "lib/trace.h"

#include "dtm0/co_fom.h"

#if 0
struct m0_dtm0_pmach_fom {
	struct m0_fom        dpf_base;
	struct m0_dtm0_pmach dpf_pmach;
	uint64_t             dpf_parent_sm_id;
};

enum co_fom_state {
	CO_FOM_INIT = M0_FOM_PHASE_INIT,
	CO_FOM_DONE = M0_FOM_PHASE_FINISH,
	CO_FOM_WAITING = M0_FOM_PHASE_NR,
	CO_FOM_FAILED,
	CO_FOM_NR,
};

static struct m0_sm_state_descr co_fom_states[] = {
	[CO_FOM_INIT] = {
		.sd_name      = "CO_FOM_INIT",
		.sd_allowed   = M0_BITS(DRF_WAITING, DRF_FAILED),
		.sd_flags     = M0_SDF_INITIAL,
	},
	/* terminal states */
	[CO_FOM_DONE] = {
		.sd_name      = "CO_FOM_DONE",
		.sd_allowed   = 0,
		.sd_flags     = M0_SDF_TERMINAL,
	},
	/* failure states */
	[CO_FOM_FAILED] = {
		.sd_name      = "CO_FOM_FAILED",
		.sd_allowed   = M0_BITS(CO_FOM_DONE),
		.sd_flags     = M0_SDF_FAILURE,
	},

	/* intermediate states */
#define _ST(name, allowed)            \
	[name] = {                    \
		.sd_name    = #name,  \
		.sd_allowed = allowed \
	}
	_ST(CO_FOM_WAITING,           M0_BITS(CO_FOM_WAITING,
					      CO_FOM_FAILED),
#undef _ST
};

const static struct m0_sm_conf co_fom_conf = {
	.scf_name      = "co_fom",
	.scf_nr_states = ARRAY_SIZE(co_fom_states),
	.scf_state     = co_fom_states,
};

static void m0_dtm0_pmach_fom_fini(struct m0_fom *base)
{
	struct m0_dtm0_pmach_fom *fom;

	M0_PRE(base != NULL);
	fom = M0_AMB(fom, base, dpf_base);
	m0_fom_fini(base);
	m0_free(fom);
}

static size_t m0_dtm0_pmach_fom_locality(const struct m0_fom *base)
{
	(void) base;
	return 1;
}

static const struct m0_fom_ops m0_dtm0_pmach_fom_ops = {
	.fo_fini = m0_dtm0_pmach_fom_fini,
	.fo_tick = m0_dtm0_pmach_tick,
	.fo_home_locality = m0_dtm0_pmach_fom_locality
};

static const struct m0_fom_type_ops m0_dtm0_pmach_fom_type_ops = {
        .fto_create = m0_dtm0_pmach_fom_create,
};

static int m0_dtm0_pmach_fom_init(struct m0_dtm0_pmach_fom *fom,
				  struct m0_dtm0_pmach     *pmach,
				  uint64_t                  parent_sm_id)
{
	struct m0_rpc_machine  *mach;
	struct m0_reqh         *reqh;
	struct dtm0_req_fop    *owned_req;

	M0_ENTRY();
	M0_PRE(fom != NULL);
	M0_PRE(svc != NULL);
	M0_PRE(req != NULL);
	M0_PRE(m0_fid_is_valid(tgt));

	m0_fom_init(&fom->dfp_base, &m0_dtm0_pmach_fom_type,
		    &m0_dtm0_pmach_fom_ops,
		    NULL, NULL, reqh);

	fom->dfp_pmach = pmach;
	fom->dfp_parent_sm_id = parent_sm_id;

	return M0_RC(0);
}

/* XXX: resource counter */
static int global_users = 0;

static void global_init(void)
{
	if (global_users++ > 0)
		return;

	m0_sm_conf_init(&co_fom_conf);

	M0_FOP_TYPE_INIT(&dtm0_net_fop_fopt,
			 .name      = "DTM0 net",
			 .opcode    = M0_DTM0_NET_OPCODE,
			 .xt        = m0_dtm0_msg_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .fom_ops   = &dtm0_net_fom_type_ops,
			 .sm        = &dtm0_net_sm_conf,
			 .svc_type  = &dtm0_service_type);
}

static void global_fini(void)
{
	if (--global_users == 0)
		m0_fop_type_fini(&dtm0_net_fop_fopt);
}
#endif



#undef M0_TRACE_SUBSYSTEM

/** @} end of co_fom group */

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
