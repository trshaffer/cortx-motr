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
#include "rpc/rpc_opcodes.h" /* M0_DTM0_CO_FOM_OPCODE */
#include "lib/memory.h" /* M0_ALLOC_PTR */
#include "reqh/reqh_service.h" /* m0_reqh_service */

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
		.sd_allowed   = M0_BITS(CO_FOM_WAITING, CO_FOM_FAILED,
					CO_FOM_DONE),
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
					      CO_FOM_DONE,
					      CO_FOM_FAILED)),
#undef _ST
};

const static struct m0_sm_conf co_fom_sm_conf = {
	.scf_name      = "co_fom",
	.scf_nr_states = ARRAY_SIZE(co_fom_states),
	.scf_state     = co_fom_states,
};

static void co_fom_fini(struct m0_fom *fom);
static int  co_fom_tick(struct m0_fom *fom);
static size_t co_fom_locality(const struct m0_fom *fom);
static const struct m0_fom_ops co_fom_ops = {
	.fo_fini = co_fom_fini,
	.fo_tick = co_fom_tick,
	.fo_home_locality = co_fom_locality
};

static const struct m0_fom_type_ops co_fom_type_ops = { .fto_create = NULL };
static struct m0_fom_type co_fom_type;
extern struct m0_reqh_service_type m0_cfs_stype;

static struct m0_co_fom *base2cf(struct m0_fom *fom)
{
	struct m0_co_fom *cf = M0_AMB(cf, fom, cf_base);
	return cf;
}

M0_INTERNAL int m0_co_fom_init(struct m0_co_fom *cf,
			       struct m0_co_fom_cfg *cf_cfg)
{
	cf->cf_cfg = *cf_cfg;
	m0_fom_type_init(&co_fom_type, M0_DTM0_CO_FOM_OPCODE,
			 &co_fom_type_ops, &m0_cfs_stype,
			 &co_fom_sm_conf);
	m0_be_op_init(&cf->cf_op);
	m0_fom_init(&cf->cf_base, &co_fom_type,
		    &co_fom_ops, NULL, NULL, cf->cf_cfg.cfc_reqh);
	return m0_co_context_init(&cf->cf_context);
}

static void co_fom_fini(struct m0_fom *fom)
{
	struct m0_co_fom *cf = base2cf(fom);
	M0_PRE(M0_IN(cf->cf_base.fo_sm_phase.sm_state,
		     (CO_FOM_INIT, CO_FOM_DONE)));

	m0_co_context_fini(&cf->cf_context);
	m0_be_op_fini(&cf->cf_op);
	m0_fom_fini(fom);
	if (cf->cf_cfg.cfc_fini_op != NULL)
		m0_be_op_done(cf->cf_cfg.cfc_fini_op);
}

M0_INTERNAL void m0_co_fom_fini(struct m0_co_fom *cf)
{
	M0_PRE(cf->cf_base.fo_sm_phase.sm_state == CO_FOM_INIT);
	co_fom_fini(&cf->cf_base);
}

static size_t co_fom_locality(const struct m0_fom *base)
{
	(void) base;
	return 1;
}

static int co_fom_tick(struct m0_fom *fom)
{
	struct m0_co_fom *cf = base2cf(fom);
	int               phase = m0_fom_phase(fom);
	int               outcome;

	M0_ENTRY("fom=%p, cf=%p, phase=%d", fom, cf, phase);

	if (phase == CO_FOM_FAILED) {
		m0_fom_phase_move(fom, fom->fo_sm_phase.sm_rc, CO_FOM_DONE);
		outcome = M0_RC(M0_FSO_WAIT);
	} else {
		M0_CO_START(&cf->cf_context);
		cf->cf_cfg.cfc_tick(cf, cf->cf_cfg.cfc_arg);
		outcome = M0_CO_END(&cf->cf_context);
		M0_POST(M0_IN(outcome, (0, M0_FSO_AGAIN, M0_FSO_WAIT)));
		if (outcome == 0) {
			m0_fom_phase_move(fom, 0, CO_FOM_DONE);
			outcome = M0_RC(M0_FSO_WAIT);
		}
	}

	return M0_RC(outcome);
}


M0_INTERNAL void m0_co_fom_start(struct m0_co_fom *cf)
{
	m0_fom_queue(&cf->cf_base);
}

M0_INTERNAL int m0_co_fom_await(struct m0_co_fom *cf)
{
	return m0_be_op_tick_ret(&cf->cf_op, &cf->cf_base, CO_FOM_WAITING);
}

M0_INTERNAL int m0_co_fom_failed(struct m0_co_fom *cf, int rc)
{
	m0_fom_phase_move(&cf->cf_base, rc, CO_FOM_FAILED);
	return M0_FSO_AGAIN;
}

/* -----8<------------------------------------------------------------------- */
/* CO_FOM service */

struct cfs_service {
	struct m0_reqh_service cfs_base;
};

static int cfs_allocate(struct m0_reqh_service           **out,
			const struct m0_reqh_service_type *stype);

static const struct m0_reqh_service_type_ops cfs_stype_ops = {
	.rsto_service_allocate = cfs_allocate
};

struct m0_reqh_service_type m0_cfs_stype = {
	.rst_name  = "co-fom-service",
	.rst_ops   = &cfs_stype_ops,
	/* Level justification: co_foms may be needed before NORMAL level. */
	.rst_level = M0_BE_TX_SVC_LEVEL,
};

M0_INTERNAL int m0_cfs_register(void)
{
	return m0_reqh_service_type_register(&m0_cfs_stype);
}

M0_INTERNAL void m0_cfs_unregister(void)
{
	m0_reqh_service_type_unregister(&m0_cfs_stype);
}

static int  cfs_start(struct m0_reqh_service *service);
static void cfs_stop(struct m0_reqh_service *service);
static void cfs_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_ops cfs_ops = {
	.rso_start = cfs_start,
	.rso_stop  = cfs_stop,
	.rso_fini  = cfs_fini
};

static int cfs_allocate(struct m0_reqh_service           **service,
			const struct m0_reqh_service_type *stype)
{
	struct cfs_service *s;

	M0_ENTRY();
	M0_PRE(stype == &m0_cfs_stype);

	M0_ALLOC_PTR(s);
	if (s == NULL)
		return M0_ERR(-ENOMEM);

	s->cfs_base.rs_ops = &cfs_ops;
	*service = &s->cfs_base;

	return M0_RC(0);
}

static void cfs_fini(struct m0_reqh_service *service)
{
	M0_ENTRY();
	m0_free(container_of(service, struct cfs_service, cfs_base));
	M0_LEAVE();
}

static int cfs_start(struct m0_reqh_service *service)
{
	M0_ENTRY();
	return M0_RC(0);
}

static void cfs_stop(struct m0_reqh_service *service)
{
	M0_ENTRY();
	M0_LEAVE();
}

M0_INTERNAL int m0_co_fom_domain_init(struct m0_co_fom_domain *cfd,
				      struct m0_reqh           *reqh)
{
	M0_PRE(cfd->cfd_svc == NULL);
	return m0_reqh_service_setup(&cfd->cfd_svc, &m0_cfs_stype,
				     reqh, NULL, NULL);
}

M0_INTERNAL void m0_co_fom_domain_fini(struct m0_co_fom_domain *cfd)
{
	m0_reqh_service_quit(cfd->cfd_svc);
	cfd->cfd_svc = NULL;
}

/* -----8<------------------------------------------------------------------- */


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
